# ASC-COMM CCU Profiling 详细设计

## 1. 背景与目标

`<<<>>>` 是 ASC-COMM 的 CCU Host 直驱入口（`asccomm_ccu_host_kernel_launch`）。不接 profiling 时，msprof 只能看到 runtime/device 侧的 task_duration，看不到这次 launch 在 Host 侧的 API 区间，也看不到 CCU 内部的任务明细。

## 2. 总体方案

### 2.1 整体profiling架构（五层）

| 层 | 位置 | 输入 | 输出 |
| --- | --- | --- | --- |
| 数据生成层 | ccu_kernel.cc / ccu_rep_context.cc | 用户构图、通信原语调用 | `std::vector<ccu_profiling_info>` |
| 数据导出层 | hcomm/resource/kernel/ccu_kernel.cc | `ccu_profiling_info` | `profiling_detail[]` |
| Host 收集层 | ccu_host_launch.cpp | launch 上下文、launch 后取数 | `ccu_prof_report_context` + `profiling_detail[]` |
| profapi 上报层 | ccu_profiling.cc | 上下文和明细 | MsprofApi / MsprofCompactInfo / MsprofAdditionalInfo |
| msprof 展示层 | msprof / profapi | 上报记录 | 时间线和通信视图 |

### 2.2 新增内容

| 文件 | 新增 | 说明 |
| --- | --- | --- |
| src/ccu/ccu_host_launch.cpp | 时间采样 + launch 后取数 | `<<<>>>` 入口处采样 begin/end，launch 成功后取明细并触发上报（§2.3） |
| src/ccu/ccu_profiling.h / .cc | 5 个上报 API、profapi 加载、开关管理、payload 组装 | 内部头，不安装为公共接口；无 CANN profapi 头时编译为降级空实现（`#else` 分支） |
| src/ccu/hcomm/.../ccu_kernel.cc | `get_ccu_profiling_details`（:3041） | 数据导出：查 kernel → `get_ccu_profiling_info()` → 逐字段拷入 `profiling_detail` |
| ccu_kernel.cc / ccu_rep_context.cc 既有逻辑 | 复用 | 明细生成（SQE/waitCKE/LoopGroup 缓存）全部复用 hcomm 迁移代码，不重建 |

#### 2.2.1 ccu_profiling

本方案为 `<<<>>>` 补 Host 侧 profiling。核心是 **5 个上报 API**（均在 `ccu_profiling.cc`，snake_case）：

| # | API（ccu_profiling.cc 行号） | 上报记录 | level | type | msprof 中看什么 |
| --- | --- | --- | ---: | ---: | --- |
| 1 | `report_ccu_node_api_and_basic_info` (:427) | Node API 耗时条 + 节点基本信息 | 10000 | 5 / 0 | Host 时间线上 `<<<>>>` 调用的起止区间 |
| 2 | `report_ccu_hccl_task_api_and_info` (:548) | HCCL Task API 耗时条 + hccl_info | 5500 | 0x010001 / 0 | 通信视图中 "Ccu" 类目的任务耗时 |
| 3 | `report_ccu_task_info` (:390) | ccu_task_info | 5500 | 14 | SQE 粒度：kernel 名、die、mission、instr |
| 4 | `report_ccu_wait_signal_info` (:468) | ccu_wait_signal_info | 5500 | 15 | WaitCKE 粒度：ckeId、mask、channel |
| 5 | `report_ccu_group_info` (:506) | ccu_group_info | 5500 | 16 | LoopGroup 粒度 |

其中 1/2 不依赖明细数量，每次 enabled 的 launch 各发一条；3/4/5 按 `get_ccu_profiling_details` 返回的真实明细逐条分发。该方案不替代已有 task_duration，而是补齐 Host 视角，使 msprof 同时看到：Host launch 区间、通信任务画像、设备侧 task_duration、CCU 内部明细。

### 2.3 一次 launch 的采集与上报流程

代码主链（`ccu_host_launch.cpp:209-239`）：

```cpp
(void)asc::ccu_profiling::init_ccu_profiling_once();          // ① 首次调用时初始化profapi
asc::ccu_profiling::ccu_prof_report_context profCtx{};         //    kernel_func/name/die_id入ctx
const bool profEnabled = asc::ccu_profiling::is_ccu_prof_report_ready();
if (profEnabled) {
    profCtx.begin_time = asc::ccu_profiling::msprof_sys_cycle_time();   // ② 采样begin
}

ret = asccomm_ccu_kernel_launch(...);                          // ③ 正常launch

if (profEnabled) {
    profCtx.end_time = asc::ccu_profiling::msprof_sys_cycle_time();    // ④ 采样end
    const CcuResult detail_ret = asc::ccu_profiling::get_ccu_profiling_details(
        kernel_handle, args, task_args_num, profiling_details);        // ⑤ launch后取数
    if (detail_ret != CCU_SUCCESS) {
        /* 只记日志, 跳过本次上报 */
    } else {
        asc::ccu_profiling::report_ccu_data_plane_after_launch(profCtx, profiling_details);  // ⑥ 上报
    }
}
return CCU_SUCCESS;
```

步骤与实现的对应（每步调的是哪个文件的哪个函数）：

| 步骤 | 调用 | 实现位置 |
| --- | --- | --- |
| ① | `init_ccu_profiling_once()` | ccu_profiling.cc:266 `InitCcuProfilingOnce`（dlopen libprofapi.so → `g_asccommCcuProfApi` 函数表 :165；`msprofRegisterCallback` 注册开关 callback，msprof 下发 START/STOP 经 `CommandHandle`→`Start/StopCcuSubscribe` 异步改变 ② 的 ready 状态，launch 主链只读不写） |
| ② | `is_ccu_prof_report_ready()` / `msprof_sys_cycle_time()` | ccu_profiling.cc:361 / 函数表 `msprofSysCycleTime` |
| ③ | `asccomm_ccu_kernel_launch` | 原 launch 链路，不经过 ccu_profiling |
| ⑤ | `get_ccu_profiling_details()` | **声明在 ccu_profiling.h，实现在数据面 ccu_kernel.cc:3041**（唯一跨模块边界）：`ccu_kernel_mgr.get_kernel(handle)` → `get_ccu_profiling_info`(:3022) 读注册期生成的明细缓存 → 逐字段拷入 `profiling_detail`（name 用 `snprintf` 定长拷贝） |
| ⑥ | `report_ccu_data_plane_after_launch()` | ccu_profiling.cc:608，内部分发见下 |

要点：

1. **launch 与 profiling 解耦**：③ 是原有链路原样；取数/上报在 launch 成功之后，任何失败只记日志，launch 返回值不受影响（⑤ 失败仅跳过上报）。
2. **取数点在 launch 后**：`get_ccu_profiling_info()` 依赖翻译后绑定的物理资源 ID（mission/cke/instr），注册期已完成翻译，此时取数安全。
3. **依赖单向**：ccu_profiling.cc 不 include 任何数据面头；`get_ccu_profiling_details` 声明在 ccu_profiling.h、实现放数据面 ccu_kernel.cc（符号在 `asc::ccu_profiling` 命名空间）——两侧只共享这一个头。ccu_host_launch.cpp 只调 ccu_profiling.h 声明的函数，不直接碰 profapi 函数表。

⑥ 的内部分发（`ccu_profiling.cc:608`）：

```cpp
report_ccu_node_api_and_basic_info(ctx, threadId);        // API#1, 不依赖task/stream
report_ccu_hccl_task_api_and_info(ctx, threadId);         // API#2, 不依赖task/stream
rtGetTaskIdAndStreamID(&taskId, &streamId);               // 查最近一次提交的task/stream
for (detail : details)                                    // (g_asccommCcuRuntimeApi, :180)
    switch (detail.type) {
        task        -> report_ccu_task_info(...)           // API#3
        wait_signal -> report_ccu_wait_signal_info(...)    // API#4
        group       -> report_ccu_group_info(...)          // API#5, 当前无group明细
    }
```

## 3. 开关与初始化

`AsccommCcuProfiling` 单例（`ccu_profiling.cc:239`）维护状态：

- **初始化**（`InitCcuProfilingOnce`，:266，首次 launch 触发）：CAS 保证只初始化一次；`dlopen("libprofapi.so")` + dlsym 七个函数 → `msprofRegisterCallback(MSPROF_MODULE_ID_HCCL=3, callback)`。任一步失败置 `UNAVAILABLE`，profiling 永久降级。
- **开关命令**（`CommandHandle`，:295）：profapi 下发 `RT_PROF_CTRL_SWITCH`。START 且 `profSwitch & PROF_TASK_TIME_L1_MASK` 非零时，`StartCcuSubscribe`（:318）注册 4 个类型（14/15/16/hccl_info，注册到 level 5500）并置 `ccuInfoTypesRegistered`；STOP 时全部复位（下次 START 重试注册）。
- **就绪判定**（`IsCcuProfReportReady`，:361）：`profApiState == INITIALIZED && enableHcclL1 && ccuInfoTypesRegistered`——即 profapi 可用 + L1 开启 + 类型已注册，三者同时成立。

taskId/streamId 查询走 `libruntime.so` 的 `rtGetTaskIdAndStreamID`（`LoadCcuRuntimeApi`，:216，优先 RTLD_NOLOAD 复用已加载库）。

失败策略一览：

| 场景 | 行为 | 代码依据 |
| --- | --- | --- |
| libprofapi 加载/注册失败 | 降级，launch 正常 | :274/:283 置 UNAVAILABLE |
| 明细获取失败 | 记日志跳过上报，launch 返回成功 | ccu_host_launch.cpp:232-236 |
| 单条上报失败 | 记日志继续后续明细 | 各 report API 返回 bool，调用处 `(void)` 忽略 |
| launch 失败 | 不采样 end_time、不上报 | ccu_host_launch.cpp:220-226 提前 return |


