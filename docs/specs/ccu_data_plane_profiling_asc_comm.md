# CCU 数据面 Profiling（asc-comm 直驱路径，无 DFX Callback）

## 1. 背景与目标

asc-comm 提供 `asccomm_ccu_host_kernel_launch` 直驱 CCU kernel 的路径。该路径不经过 hcomm 的
DFX task callback（`ThreadHandle` 未绑定 callback），因此 CCU 任务无法进入 hcomm 的标准
profiling 链路（`HcclCommDfx` → `MirrorTaskManager` → `DfxProfilingReporter`）。

本方案的目标：**只在 asc-comm 仓内修改**，通过 `dlopen` 直接调用 msprof 的 profapi 接口，
在 launch 前后完成 profiling 注册与数据面上报，不依赖 hcomm 仓的任何 DFX 模块。

- 实现文件：`src/ccu/ccu_host_launch.cpp`（全部改动集中于此文件 + UT 构建配置）
- 参考实现：hcomm `src/legacy/ascend950/framework/dfx/profiling/profiling_handler.cc`
  （`ProfilingHandler::Init()` / `GetCcuTaskInfo()`）
- "仅数据面"含义：只上报 asc-comm 调用路径上实际可获得的信息；数据面没有来源的字段一律
  填协议非法值（解析端按 unknown 呈现），不伪造合法值。

## 2. 总体流程

```text
                        ┌──────────────────────────────────────────┐
                        │ asccomm_ccu_host_kernel_launch(...)      │
                        │  (或 HcommCcuHostKernelLaunch 兼容入口)   │
                        └──────────────┬───────────────────────────┘
                                       │ 校验cfg / weak符号可用性
                                       ▼
                        ┌──────────────────────────────────────────┐
                        │ GetKernelHandleCache().Match(...)        │
                        │  注册(或命中缓存) kernel → kernelHandle   │
                        └──────────────┬───────────────────────────┘
                                       ▼
                ┌──────────────────────────────────────────────────┐
        ┌──────▶│ LaunchCcuKernelWithStream(handle, args, n,       │
        │       │   stream, kernelFunc, phyDieMask)                │
        │       ├──────────────────────────────────────────────────┤
        │       │ 1. GetThreadHandleCache().GetOrCreate(stream)    │
        │       │ 2. InitCcuProfilingOnce()          ← 首次launch  │
        │       │ 3. IsCcuProfReportReady()?         ← L1开关判断  │
        │       │    └─ yes: beginTime = msprofSysCycleTime()      │
        │       │ 4. HcommCcuKernelLaunch(...)       ← 真正提交    │
        │       │ 5. 成功且profEnabled:               │
        │       │    └─ endTime = msprofSysCycleTime()             │
        │       │       ReportCcuDataPlaneAfterLaunch(profCtx)     │
        │       │        ├─ rtGetTaskIdAndStreamID() (libruntime)  │
        │       │        └─ ReportCcuTaskInfo() → profapi type 14  │
        │       └──────────────────────────────────────────────────┘
        │                       ▲
        └─── START/STOP 命令    │
            (profapi 下发回调)  │
```

整个机制分三段：**初始化注册**（谁、什么时候把回调挂到 profapi）、**开关控制**（msprof 如何
通知 asc-comm 开/关采集）、**数据上报**（launch 后发什么、怎么发）。

## 3. 模块与数据结构

### 3.1 编译期自适应：`ASCCOMM_CCU_HAS_PROFAPI`

```cpp
#if defined(__has_include)
#if __has_include("aprof_pub.h") && __has_include("rt_external_base.h")
#include "aprof_pub.h"        // ProfCommandHandle / Msprof* 结构 / PROF_TASK_TIME_L1_MASK
#include "rt_external_base.h" // rtProfCommandHandle_t / RT_PROF_CTRL_SWITCH / rtGetTaskIdAndStreamID
#define ASCCOMM_CCU_HAS_PROFAPI 1
#endif
#endif
```

- 头文件来自 CANN 安装目录 `pkg_inc/profiling`、`pkg_inc/runtime`（见 §6 构建配置）；
- **没有 CANN 头的编译环境：整个 profiling 块被裁掉**，`ccu_host_launch.cpp` 保持纯
  launch 逻辑，行为与改动前完全一致；
- 所有 profiling 代码都包裹在 `#ifdef ASCCOMM_CCU_HAS_PROFAPI` 内。

### 3.2 payload 结构定义（本地复制，与 hcomm 逐字一致）

CANN 头文件中**没有** `MsprofCcuTaskInfo` 等结构——它们是 hcomm 自定义、通过
`MsprofAdditionalInfo.data[]`（固定 232 字节）透传给 profapi 的。本文件按 hcomm
`profiling_handler.h` 的原始定义复制了三个结构：

| type | 结构 | 注册名 | 内容 |
| ---: | --- | --- | --- |
| 14 | `MsprofCcuTaskInfo` | `ccu_task_info` | Task 粒度（die/mission/instrId） |
| 15 | `MsprofCcuWaitSignalInfo` | `ccu_wait_signal_info` | WaitCKE（ckeId/mask/channel） |
| 16 | `MsprofCcuGroupInfo` | `ccu_group_info` | LoopGroup（reduceOp/dataType/dataSize/channel） |

并有编译期保证：

```cpp
static_assert(sizeof(MsprofCcuTaskInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(MsprofCcuGroupInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
static_assert(sizeof(MsprofCcuWaitSignalInfo) <= MSPROF_ADDTIONAL_INFO_DATA_LENGTH);
```

结构布局与 SDK 解析端的一致性由"同一份 hcomm 定义原样复制 + 同一 libprofapi.so"保证。

### 3.3 函数指针表与动态加载

两组 dlopen 加载的 API，互不依赖、各自独立降级：

```cpp
struct AsccommCcuProfApi {       // ← dlopen("libprofapi.so")
    msprofRegisterCallback       // 注册开关回调
    msprofRegTypeInfo            // 注册上报类型（14/15/16）
    msprofReportAdditionalInfo   // 发送 payload
    msprofReportApi              // Node API 耗时条（type=LAUNCH）
    msprofReportCompactInfo      // 节点基本信息（type=BASIC_INFO）
    msprofStr2Id                 // 字符串 → hash itemId
    msprofSysCycleTime           // 统一时钟（begin/end/时间戳）
};

struct AsccommCcuRuntimeApi {    // ← dlopen("libruntime.so")
    rtGetTaskIdAndStreamID       // 查询当前线程最近提交 task 的 id/streamId
};
```

- `LoadCcuProfApi()`：`dlopen("libprofapi.so", RTLD_NOW | RTLD_GLOBAL)`，7 个符号必须
  全部解析成功，否则整体返回 false；
- `LoadCcuRuntimeApi()`：先 `RTLD_NOLOAD` 复用进程内已加载的 libruntime（训练进程必然
  已加载，避免重复 dlopen），失败再正常 dlopen；
- 注意 `rtGetTaskIdAndStreamID` 与 hcomm 的 `hrtGetTaskIdAndStreamID`（`adapter_rts.cc`
  中的薄封装）等价，语义是"当前线程**最近一次**提交的 task"——因此必须在
  `HcommCcuKernelLaunch` 返回**之后**调用才能拿到刚提交的 CCU task 的 ID。

### 3.4 控制块：`AsccommCcuProfiling` 单例

```cpp
struct AsccommCcuProfiling {
    std::atomic<uint32_t> profApiState;      // 三态: 见下
    std::atomic<bool> enableHcclL0;          // L0 开关（当前仅记录）
    std::atomic<bool> enableHcclL1;          // L1 开关（上报的硬性条件）
    std::atomic<bool> ccuInfoTypesRegistered; // 14/15/16 是否注册成功
    std::mutex ccuMutex;                     // 保护类型注册过程
};
```

| profApiState | 含义 | 后续行为 |
| --- | --- | --- |
| `UNINITIALIZED`(0) | 从未初始化 | 下次 launch 重试 Init |
| `INITIALIZED`(1) | profapi 加载 + 回调注册成功 | 正常工作 |
| `UNAVAILABLE`(2) | dlopen/dlsym/RegisterCallback 任一失败 | **永久降级**，不再重试 |

### 3.5 调试日志：`CcuProfDebugLog`

```cpp
static const bool enabled = (std::getenv("ASCCOMM_CCU_PROF_DEBUG") != nullptr);
```

- 环境变量 `ASCCOMM_CCU_PROF_DEBUG` 任意非空值开启；输出到 **stderr**（前缀
  `[ASCCOMM_CCU_PROF]`），不依赖 dlog 日志级别配置，训练进程里最容易观察；
- 未设置时零输出、零开销（`static const bool` 一次判断）；
- 与之并存的 `ASCCOMM_CCU_LOG_ERROR`（dlog，模块 ID 5=CCU）用于正式错误日志。

## 4. 注册流程（第一阶段）

### 4.1 `InitCcuProfilingOnce()` —— 首次 launch 触发

调用点：`LaunchCcuKernelWithStream` 中、`HcommCcuKernelLaunch` 之前：

```cpp
#ifdef ASCCOMM_CCU_HAS_PROFAPI
    (void)InitCcuProfilingOnce();
#endif
```

内部逻辑（对标 hcomm `ProfilingHandler::Init()`）：

```text
CAS: UNINITIALIZED → INITIALIZED          (只允许一个线程真正执行)
  ├─ LoadCcuProfApi()                     失败 → UNAVAILABLE, 打日志, return false
  ├─ callback = [](rtType, data, len) { AsccommCcuProfiling::GetInstance().CommandHandle(...) }
  ├─ msprofRegisterCallback(moduleId=HCCL(3), callback)
  │                                       失败 → UNAVAILABLE, 打日志, return false
  └─ 成功 → debug日志 "Init OK"
```

要点：

- **幂等**：CAS 抢占后其余线程只做一次 atomic 读；热路径开销可忽略；
- **失败不影响 launch**：`(void)` 丢弃返回值，所有失败路径只打日志；
- **moduleId**：`HCCL = 3`（CANN `log_types.h` 模块枚举，与 hcomm
  `ProfilingHandler::Init()` 使用的值相同）。同进程 hcomm 也注册 moduleId=3 时，
  重复注册行为取决于 profapi 实现——按"失败即降级"处理，已验证单 asc-comm 场景可正常注册。

### 4.2 已知时序约束

注册时机是"**第一次 CCU kernel launch**"，晚于 hcomm（在 communicator 初始化时注册）。
若 msprof 的 START 命令在此前已下发，本模块会错过该命令且 profapi 不会补发——表现为
debug 日志只有 `Init OK` 而无 `CommandHandle`。如需支持"launch 前已开采集"的场景，
可将注册提前到 `.so` 加载时（`__attribute__((constructor))` 调 `InitCcuProfilingOnce`）。

## 5. 开关控制流程（第二阶段：profapi → asc-comm）

### 5.1 `CommandHandle(rtType, data, len)` —— profapi 回调入口

```text
CommandHandle(rtType, data, len)
  ├─ data==nullptr || rtType != RT_PROF_CTRL_SWITCH → 返回 1 (HCCL_E_PARA)
  └─ data → rtProfCommandHandle_t*，取 type 与 profSwitch
      ├─ type = PROF_COMMANDHANDLE_TYPE_START → StartCcuSubscribe(profSwitch)
      ├─ type = PROF_COMMANDHANDLE_TYPE_STOP  → StopCcuSubscribe()
      └─ 其他 → 忽略，返回 0
```

### 5.2 `StartCcuSubscribe(profSwitch)` —— L1 命中才激活

```text
StartCcuSubscribe(profSwitch)
  ├─ (profSwitch & PROF_TASK_TIME_L1_MASK) == 0 → 直接返回
  │     （只开 L0 不注册 14/15/16，与 hcomm 行为一致）
  └─ lock(ccuMutex)
      ├─ enableHcclL0 = true; enableHcclL1 = true
      ├─ ccuInfoTypesRegistered 已置位 → 返回（重复 START 幂等）
      └─ 依次注册:
          msprofRegTypeInfo(level=5500(MSPROF_REPORT_HCCL_NODE_LEVEL), 14, "ccu_task_info")
          msprofRegTypeInfo(level=5500,                            15, "ccu_wait_signal_info")
          msprofRegTypeInfo(level=5500,                            16, "ccu_group_info")
          单条失败 → 打日志返回（保持未注册状态，下次 START 重试）
          全部成功 → ccuInfoTypesRegistered = true
```

### 5.3 `StopCcuSubscribe()`

```text
enableHcclL0 = false; enableHcclL1 = false; ccuInfoTypesRegistered = false
```

## 6. 上报流程（第三阶段）

### 6.1 launch 路径埋点

`LaunchCcuKernelWithStream` 签名扩展，从上层传入 `kernelFunc`（kernel 函数指针，
itemId 回退标识）、`kernelName`（kernel 名字符串，itemId 首选来源，可为空）和
`phyDieMask`（算 dieId）：

```cpp
    // launch 前
    CcuProfReportContext profCtx{ kernelFunc, kernelName, GetDieIdByMask(phyDieMask) };
    const bool profEnabled = IsCcuProfReportReady();      // 三个atomic读
    if (profEnabled) {
        profCtx.beginTime = msprofSysCycleTime();
    }

    CcuResult launchRet = HcommCcuKernelLaunch(...);       // 真正提交

    // launch 后
    if (profEnabled && launchRet == CCU_SUCCESS) {
        profCtx.endTime = msprofSysCycleTime();
        ReportCcuDataPlaneAfterLaunch(profCtx);
    }
    return launchRet;
```

关键时序约束：

- **上报只在 launch 成功后执行**：launch 失败不产生 payload；
- **`rtGetTaskIdAndStreamID` 必须在 `HcommCcuKernelLaunch` 返回后调用**（查询的是当前
  线程最近一次提交的 task，与 hcomm `ReportCcuTaskDfx` 的时序一致）；
- profiling 关闭时（`IsCcuProfReportReady()` 为 false），launch 路径只增加 3 次 atomic
  读，无 dlopen/无 profapi 调用。

### 6.2 `ReportCcuDataPlaneAfterLaunch(ctx)`

```text
ReportCcuDataPlaneAfterLaunch(ctx)
  ├─ IsCcuProfReportReady() 再次确认（START/STOP 可能已切换）
  ├─ ReportCcuNodeApiAndBasicInfo(ctx, gettid())   ← Node粒度, 不依赖taskId/streamId
  ├─ g_asccommCcuRuntimeApi.rtGetTaskIdAndStreamID == nullptr?
  │    └─ 首次: LoadCcuRuntimeApi()，失败 → skip（打日志）
  ├─ rtGetTaskIdAndStreamID(&taskId, &streamId)
  │    失败 → skip（打日志，不影响 launch 返回值）
  ├─ ReportCcuTaskInfo(ctx, streamId, taskId, gettid())        ← type 14
  └─ ReportCcuWaitSignalInfo / ReportCcuGroupInfo               ← type 15/16, 空值填充
```

### 6.2a `ReportCcuNodeApiAndBasicInfo(...)` —— Node 粒度 API 耗时条与节点基本信息

对齐 hcomm `DfxProfilingHandler::ReportNodeApi` / `ReportNodeBasicInfo`（由
`ReportHostApi` 统一触发的那对上报）。直驱路径的"API"即 `asccomm_ccu_host_kernel_launch`
本身，`beginTime/endTime` 为 launch 前后采样值。**门控统一挂 L1**（与 type 14 同条件）；
hcomm 侧 NodeApi 在仅 L0 时也上报，asc-comm 为复用现有采样条件收敛为 L1-only，仅开 L0
时无此两类数据（已知口径差）。

上报 1 —— Node API 耗时条（`MsprofApi` → `msprofReportApi`）：

| 字段 | 取值 |
| --- | --- |
| `level` | `MSPROF_REPORT_NODE_LEVEL`(10000) |
| `type` | `MSPROF_REPORT_NODE_LAUNCH_TYPE`(5) |
| `threadId` | `syscall(SYS_gettid)` |
| `beginTime` / `endTime` | launch 前后采样值（`msprofSysCycleTime`） |
| `itemId` | 与 type 14 相同规则（kernel 名优先，空名回退地址 hash） |

上报 2 —— 节点基本信息（`MsprofCompactInfo` → `msprofReportCompactInfo`）：

| 字段 | 取值 |
| --- | --- |
| `level` | `MSPROF_REPORT_NODE_LEVEL`(10000) |
| `type` | `MSPROF_REPORT_NODE_BASIC_INFO_TYPE`(0) |
| `threadId` | `syscall(SYS_gettid)` |
| `dataLen` | `sizeof(MsprofNodeBasicInfo)` |
| `timeStamp` | `ctx.endTime` |
| `nodeBasicInfo.opName` / `opType` | itemId（与 type 14 同规则） |
| `nodeBasicInfo.taskType` | `MSPROF_GE_TASK_TYPE_HCCL` |
| `nodeBasicInfo.blockDim` / `opFlag` | 0（与 hcomm 一致） |

结构定义（`MsprofApi`/`MsprofCompactInfo`/`MsprofNodeBasicInfo`）全部来自公共头
`aprof_pub.h`，无需本地复制；上报顺序 NodeApi → NodeBasicInfo → type 14（先画 API 条、
再挂节点标识、最后补 task 详情）。失败只打日志，不影响 launch 返回值。

### 6.3 `ReportCcuTaskInfo(...)` —— type 14 payload 组装与发送

| 字段 | 取值 | 说明 |
| --- | --- | --- |
| `level` | `MSPROF_REPORT_HCCL_NODE_LEVEL`(5500) | 与 hcomm 一致 |
| `type` | 14 (`MSPROF_REPORT_CCU_TASK_INFO`) | 当前只上报 Task 粒度 |
| `threadId` | `syscall(SYS_gettid)` | launch 线程 |
| `dataLen` | `sizeof(MsprofCcuTaskInfo)` | static_assert 已保证不越界 |
| `timeStamp` | `ctx.endTime` | `msprofSysCycleTime` 时钟 |
| `version` | 0 | 与 hcomm 一致 |
| `workFlowMode` | `HCCL_WORKFLOW_MODE_OP_BASE`(1) | 与 hcomm 一致 |
| `itemId` | `msprofStr2Id(kernelName)`；`kernelName` 为空时回退 `msprofStr2Id(snprintf("%p", kernelFunc))` | 见下文 itemId 说明 |
| `groupName` | `DFX_INVALID_U64` | **无来源 → 非法值** |
| `rankId` | `INVALID_VALUE_RANKID`(0xFFFFFFFF) | **无来源 → 非法值** |
| `ranksize` | `INVALID_VALUE_RANKSIZE`(0xFFFFFFFF) | **无来源 → 非法值** |
| `streamId` | runtime 查询值（截断为 uint16） | 关联刚提交的 task |
| `taskId` | runtime 查询值 | 同上 |
| `dieId` | `GetDieIdByMask(cfg->ccu_schd.phy_die_mask)` | die0=0 / die1=1 |
| `missionId` | `CCU_PROF_INVALID_MISSION_ID`(0xFF) | **无来源 → 非法值** |
| `instrId` | `CCU_PROF_INVALID_INSTR_ID`(0xFFFF) | **无来源 → 非法值** |

发送：`msprofReportAdditionalInfo(1, &reporterData, sizeof(MsprofAdditionalInfo))`，
返回非 0 只打 `ASCCOMM_CCU_LOG_ERROR`，**不影响 launch 返回值**。

### 6.3a `ReportCcuWaitSignalInfo / ReportCcuGroupInfo` —— type 15/16 payload 组装与发送

两个 payload 的公共字段与 type 14 同源（`CcuAdditionalInfoCommonFields`：itemId =
kernel 名 hash、streamId/taskId = runtime 查询值、dieId = die mask 换算、timeStamp =
endTime、threadId = gettid），业务字段按需求以**默认空值**填充：

**type 15 `ccu_wait_signal_info`（`MsprofCcuWaitSignalInfo`）独有字段：**

| 字段 | 取值 | 说明 |
| --- | --- | --- |
| `ckeId` | `INVALID_UINT`(0xFFFFFFFF) | 无来源 → 非法值 |
| `mask` | 0 | 无来源 → 空值 |
| `channelId[16]` | 全 `INVALID_VALUE_CHANNELID`(0xFFFF) | 与 hcomm `CcuProfilingInfo` 初始化一致 |
| `remoteRankId[16]` | 全 `INVALID_VALUE_RANKID`(0xFFFFFFFF) | 同上 |
| `groupName/rankId/ranksize/missionId/instrId` | 与 type 14 相同非法值 | 无来源 |

**type 16 `ccu_group_info`（`MsprofCcuGroupInfo`）独有字段：**

| 字段 | 取值 | 说明 |
| --- | --- | --- |
| `reduceOpType` / `inputDataType` / `outputDataType` | `INVALID_TYPE_VALUE`(0xFF) | 无来源 → 非法值 |
| `dataSize` | 0 | 无来源 → 空值 |
| `channelId[16]` / `remoteRankId[16]` | 同 type 15 | 无来源 → 非法值 |
| `groupName/rankId/ranksize/missionId/instrId` | 与 type 14 相同非法值 | 无来源 |

发送通道与失败处理同 type 14（`msprofReportAdditionalInfo`，失败只记日志）。上报顺序：
type 14 → type 15 → type 16，三条记录通过相同 itemId 关联为同一次 launch。

**itemId 说明**：`asccomm_ccu_host_kernel_launch` 已扩展 `const char* kernel_name` 入参
（编译器直调转义传入 kernel 函数名字符串）。`kernel_name` 非空时 itemId 为
`msprofStr2Id(kernel_name)`——msprof 按 Str2Id 的字符串表呈现语义名，且与 hcomm 标准
路径同名 kernel 的 itemId 一致；同一 kernel 的所有 launch 得到相同 itemId。`kernel_name`
为空（旧调用方 / 异常路径）时回退到 kernel 函数指针格式化字符串的 hash，此时 msprof
界面呈现为地址形式的标识而非语义名。

**type 15/16 以默认空值上报**：WaitCKE（`ccu_wait_signal_info`）与 LoopGroup
（`ccu_group_info`）在 asc-comm 直驱路径上无业务字段来源（CCU kernel 的 taskArgs 协议中
不含 ckeId/mask/reduceOp/dataType/dataSize/channel 等信息），按需求以**默认空值**填充上报
——类型类字段填非法值（`INVALID_TYPE_VALUE`=0xFF、channelId=0xFFFF、remoteRankId=0xFFFFFFFF，
与 hcomm `CcuProfilingInfo` 初始化约定一致），数值类字段填 0（dataSize/mask）。公共字段
（itemId/streamId/taskId/dieId/threadId/timeStamp）与 type 14 同源。msprof 端这两个记录
的业务明细呈现为 unknown/空，但记录本身存在且可与 type 14 通过相同 itemId 关联。

### 6.4 与 hcomm 路径的互斥关系

- 本上报路径只在 **asc-comm 直驱**（`asccomm_ccu_host_kernel_launch` /
  `HcommCcuHostKernelLaunch`）时生效；
- 走 hcomm 标准 launch（`HcommCcuKernelLaunch` 由 hcomm 内部 API 直接调用）时 asc-comm
  侧代码不参与，不存在重复上报；
- 同进程同时存在两条路径时，各自的 `rtGetTaskIdAndStreamID` 查询以调用先后为准，
  asc-comm 的查询紧跟自己的 `HcommCcuKernelLaunch` 返回，取得的是自己刚提交的 task。

## 7. 构建与验证

### 7.1 构建配置

- **so 构建**（`src/ccu/CMakeLists.txt`）：SYSTEM include 已含
  `pkg_inc`、`pkg_inc/runtime` 等（`asccomm_collect_header_dirs` 递归收集），
  `aprof_pub.h`/`rt_external_base.h` 可解析 → profiling 代码编入；
- **UT 构建**（`tests/ut/CMakeLists.txt`）：需显式包含
  `${ASCEND_CANN_ARCH_DIR}/pkg_inc/profiling`（`aprof_pub.h` 在该子目录），并链接
  `${CMAKE_DL_LIBS}`（dlopen/dlsym）——UT 配置中缺一会导致 profiling 代码被
  `__has_include` 裁掉或链接失败；
- 无 CANN 头环境：`__has_include` 判定失败，编译产物与改动前行为一致。

### 7.2 已通过的验证

```bash
# so 构建（-Wall -Werror）
cmake -S . -B build/ccu_prof_check \
  -DASCCOMM_HCOMM_SOURCE_DIR=<hcomm路径> \
  -DASCEND_CANN_PACKAGE_PATH=/home/developer/Ascend/cann-9.2.0
cmake --build build/ccu_prof_check --target asccomm_ccu -j8

# UT（tests/ut/ccu/test_ccu_host_launch.cpp, 10/10 通过）
cmake -S tests/ut -B build/ut_ccu_prof \
  -DASCEND_CANN_PACKAGE_PATH=/home/developer/Ascend/cann-9.2.0 \
  -DCANN_3RD_LIB_PATH=<asc-comm>/third_party \
  -DTEST_MOD=asccomm_ut_ccu_host_launch
cmake --build build/ut_ccu_prof --target asccomm_ut_ccu_host_launch -j8

# 无 profapi 头的降级编译
g++ -std=c++17 -fsyntax-only -I include -I <仅含dlog头的目录> src/ccu/ccu_host_launch.cpp
```

### 7.3 端到端验证方法

```bash
export ASCCOMM_CCU_PROF_DEBUG=1
# 跑会触发 CCU kernel launch 的训练/demo（真实 libprofapi.so 在 LD_LIBRARY_PATH 中）
# 预期 debug 日志顺序:
#   [ASCCOMM_CCU_PROF] Init OK: profapi callback registered (moduleId=3), ...
#   （开启 msprof L1 采集后）
#   [ASCCOMM_CCU_PROF] CommandHandle: rtType=1
#   [ASCCOMM_CCU_PROF] CommandHandle: type=1 profSwitch=0x...
#   [ASCCOMM_CCU_PROF] StartCcuSubscribe: RegTypeInfo type=14(ccu_task_info) OK.
#   [ASCCOMM_CCU_PROF] StartCcuSubscribe: RegTypeInfo type=15(ccu_wait_signal_info) OK.
#   [ASCCOMM_CCU_PROF] StartCcuSubscribe: RegTypeInfo type=16(ccu_group_info) OK.
#   [ASCCOMM_CCU_PROF] StartCcuSubscribe: ALL DONE, ..., CCU profiling ACTIVE.
#   （每次 CCU kernel launch 成功后）
#   [ASCCOMM_CCU_PROF] ReportCcuNodeApi: level=10000 type=5(MSPROF_REPORT_NODE_LAUNCH_TYPE) itemId=0x... beginTime=... endTime=... name=<kernel名>, ret=0.
#   [ASCCOMM_CCU_PROF] ReportCcuNodeBasicInfo: level=10000 type=0(MSPROF_REPORT_NODE_BASIC_INFO_TYPE) itemId=0x... taskType=9(MSPROF_GE_TASK_TYPE_HCCL) timeStamp=... name=<kernel名>, ret=0.
#   [ASCCOMM_CCU_PROF] ReportCcuTaskInfo: type=14 itemId=0x... streamId=N taskId=N dieId=N name=<kernel名>, ret=0.
#   [ASCCOMM_CCU_PROF] ReportCcuWaitSignalInfo: type=15 itemId=0x... streamId=N taskId=N dieId=N name=<kernel名> (no-source fields filled with invalid defaults), ret=0.
#   [ASCCOMM_CCU_PROF] ReportCcuGroupInfo: type=16 itemId=0x... streamId=N taskId=N dieId=N name=<kernel名> (no-source fields filled with invalid defaults), ret=0.
#   （停止采集后）
#   [ASCCOMM_CCU_PROF] StopCcuSubscribe: CCU profiling DEACTIVATED.
```

**`ReportCcuTaskInfo ... ret=0` 出现即代表 payload 真正到达 profapi，链路闭环**；
NodeApi/NodeBasicInfo/WaitSignal/GroupInfo 各行同理，`ret=0` 即对应通道送达。
启动阶段三条 `RegTypeInfo type=15/16` 的注册从此有了实际消费者。

### 7.4 日志异常排查对照表

| 现象 | 结论 | 排查方向 |
| --- | --- | --- |
| 只有 `Init OK`，无 `CommandHandle` | 注册成功但 profapi 未下发开关 | 采集是否真的开启；START 是否早于首次 launch（§4.2 时序） |
| `CommandHandle` 有，`L1 not set` | 回调链路通，采集级别不含 L1 | 提高 msprof 采集级别 |
| `Init FAILED: MsprofRegisterCallback ret=N` | 注册被拒 | 大概率 hcomm 已注册 moduleId=3，需确认 profapi 重复注册语义 |
| `ReportCcuNodeApi / NodeBasicInfo ret!=0` | profapi 拒收 Node 通道 payload | 确认 libprofapi 版本含 `MsprofReportApi`/`MsprofReportCompactInfo`；检查 `MsprofApi`/`MsprofCompactInfo` 布局 |
| `ReportCcuTaskInfo ... ret!=0` | profapi 拒收 payload | 检查 dataLen/结构布局与 SDK 版本匹配 |

## 8. 已知限制与后续演进

1. **注册时机晚于进程启动**：当前在首次 launch 时注册，可能错过更早的 START 命令；
   需要时可用 `__attribute__((constructor))` 提前到 so 加载时；
2. **`kernel_name` 为空时 itemId 回退为地址 hash**：launch 入参已支持 `kernel_name`
   （编译器直调自动传入），正常路径 msprof 可呈现语义名；旧调用方传 `nullptr` 时退化为
   地址 hash 标识；
3. **type 15/16 业务字段为默认空值**：WaitCKE/LoopGroup 的业务字段（ckeId/mask/
   reduceOp/dataType/dataSize/channel 等）在直驱路径无来源，以非法值/0 填充上报（见
   §6.3）；后续若 kernel 参数协议扩展（携带 missionId/instrId/dataSize/channel 信息），
   可替换为真实值；
4. **`missionId/instrId` 为非法值**：msprof 界面上 CCU task 无法按 mission/instr 细分；
5. **模块级降级不可恢复**：`UNAVAILABLE` 后不再重试（避免每次 launch 都 dlopen）；
   若需要运行期重试语义，可将降级改为限频重试；
6. **与 hcomm 的 moduleId 竞争**：同进程共存场景下 moduleId=3 的二次注册行为未定；
   如确认冲突，需为 asc-comm 申请独立 moduleId；
7. **NodeApi/NodeBasicInfo 仅 L1 上报**：hcomm 侧 NodeApi 在仅开 L0 时也上报
   （`enableHostApi_` 门控，任意 START 即开）；asc-comm 为复用现有 begin/end 采样条件
   （仅 L1 时采样）统一挂 L1。仅开 L0 的场景 asc-comm 无 API 耗时条；如需对齐 hcomm
   口径，需增加独立 `enableHostApi` 开关并把采样条件提前。

## 9. 代码位置索引

全部实现位于 `src/ccu/ccu_host_launch.cpp`（约 280 行 profiling 代码，均包裹在
`#ifdef ASCCOMM_CCU_HAS_PROFAPI` 内）：

| 功能 | 函数/结构 |
| --- | --- |
| 头自适应 | `ASCCOMM_CCU_HAS_PROFAPI` 宏（文件头部） |
| payload 结构 | `MsprofCcuTaskInfo` / `MsprofCcuGroupInfo` / `MsprofCcuWaitSignalInfo` |
| profapi 加载 | `AsccommCcuProfApi` / `LoadCcuProfApi()` |
| runtime 加载 | `AsccommCcuRuntimeApi` / `LoadCcuRuntimeApi()` |
| 控制块 | `AsccommCcuProfiling`（单例） |
| 调试日志 | `CcuProfDebugLog()`（env `ASCCOMM_CCU_PROF_DEBUG`） |
| 初始化注册 | `InitCcuProfilingOnce()` |
| 开关处理 | `AsccommCcuProfiling::CommandHandle/StartCcuSubscribe/StopCcuSubscribe` |
| 上报入口 | `IsCcuProfReportReady()` / `ReportCcuDataPlaneAfterLaunch()` |
| Node 粒度上报 | `ReportCcuNodeApiAndBasicInfo()`（API 耗时条 + 节点基本信息） |
| payload 组装 | `ReportCcuTaskInfo()`（type 14）/ `ReportCcuWaitSignalInfo()`（type 15，空值填充）/ `ReportCcuGroupInfo()`（type 16，空值填充）/ `CcuAdditionalInfoCommonFields`（15/16 公共字段） |
| launch 埋点 | `LaunchCcuKernelWithStream()`（`HcommCcuKernelLaunch` 前后） |
