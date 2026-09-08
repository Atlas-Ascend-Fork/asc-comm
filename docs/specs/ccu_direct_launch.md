# CCU Kernel `<<<>>>` 直调实现说明

## 1. 文档范围

本文说明 asc-comm 中 CCU Kernel 使用三尖括号调用时，调用表达式如何从 ASC 源码进入 Host Runtime，再进入 Hcomm 的 CCU Kernel 注册、指令翻译和 Runtime 提交流程。

本文以样例 `examples/ccu/ccu_direct/01_allgather/main.asc` 为主线，覆盖以下内容：

- `<<<schd, insHandle, stream>>>` 的语义和编译器处理边界；
- `__ccu_host__` Kernel 在注册阶段的执行方式；
- asc-comm Host Runtime 如何桥接编译器生成的入口；
- Hcomm 如何把 Kernel 函数体转换为 CCU 指令；
- `ccu::LoadArg` 与 Host `taskArgs` 的对应关系；
- `rtCCULaunch` 的提交字段、缓存行为和当前限制；
- 与普通 AICore/AIV Kernel launch 的区别及调试方法。

本文描述的是当前代码的实现事实。编译器生成的内部 Host Stub 名称和参数布局属于 bisheng 实现细节，不应被用户代码当作稳定 ABI 直接依赖。

## 2. 一句话结论

CCU 的三尖括号调用不是把 CCU 算法作为一个普通设备 Kernel 启动。它是一条“编译器语法糖 + Host 侧注册/翻译 + Runtime 提交”的路径：

```text
CCU Kernel<<<schd, ccuIns, stream>>>(taskArgs...)
    -> bisheng 识别 CCE Kernel Call 并生成 Host 调用桩
    -> asccomm_ccu_host_kernel_launch(kernelFunc, cfg, packedArgs)
    -> HcommCcuKernelRegisterStart
    -> HcommCcuKernelRegister
    -> 执行 __ccu_host__ 函数体，生成 CCU Rep 和资源需求
    -> HcommCcuKernelRegisterEnd，翻译 Rep 为 CCU 指令
    -> CcuKernel::GeneTaskParams，填充任务参数
    -> rtCcuTaskInfo_t
    -> rtCCULaunch(taskInfo, stream)
```

其中，注册阶段执行的是 Host 侧 CCU 算法描述代码；真正的通信指令在最后由 CCU 硬件通过 `rtCCULaunch` 执行。

## 3. 示例入口和调用形式

样例在 [main.asc](../../../examples/ccu/ccu_direct/01_allgather/main.asc:574) 声明 CCU Host Kernel：

```cpp
__ccu_host__ void CcuAllGatherMesh1DMem2MemKernel(
    uint64_t sqeArgs1, uint64_t sqeArgs2, uint64_t sqeArgs3, uint64_t sqeArgs4,
    uint64_t sqeArgs5, uint64_t sqeArgs6, uint64_t sqeArgs7, uint64_t sqeArgs8,
    uint64_t sqeArgs9, uint64_t sqeArgs10, CcuKernelArg arg)
{
    // 根据 arg 获取通道和变量资源
    // 使用 ccu::LoadArg、ccu::Write、ccu::NotifyWait 等 API 描述任务
}
```

Host 侧准备 10 个 64 位任务参数和一个注册期参数结构，然后调用：

```cpp
asccomm_ccu_schd schd = {1, 0, dieMask, 0};
CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, stream>>>(
    taskArgs[0], taskArgs[1], taskArgs[2], taskArgs[3], taskArgs[4],
    taskArgs[5], taskArgs[6], taskArgs[7], taskArgs[8], taskArgs[9],
    kernelArg.get());
```

对应的完整调用位置见 [main.asc](../../../examples/ccu/ccu_direct/01_allgather/main.asc:692)。调用参数可以分为两类：

| 参数 | 作用 | 使用时机 |
| --- | --- | --- |
| `schd` | mission 数量、目标 Die、可选缓存标签 | Host launch 配置 |
| `insHandle` | CCU Instance 句柄，提供资源仓 | Kernel 注册和翻译 |
| `stream` | ACL Runtime Stream | `rtCCULaunch` 提交 |
| `taskArgs[]` | 地址、token、偏移、长度和循环参数 | 每次任务提交 |
| `kernelArg` | 通道句柄、rank 等资源拓扑信息 | 注册阶段构建 Kernel |

`taskArgs[]` 和 `kernelArg` 的生命周期语义不同：`kernelArg` 被 trampoline 在注册阶段传给算法函数，`taskArgs[]` 在 launch 阶段被复制到 CCU SQE 参数区。

## 4. 编译器层：三尖括号如何变成 Host 调用

### 4.1 ASC 编译入口

ASC 源文件通过 bisheng 编译。asc-devkit 的 ASC CMake 规则在 [CMakeASCInformation.cmake](../../../../asc-devkit/cmake/asc/asc_modules/CMakeASCInformation.cmake:121) 中将单个 ASC 源文件交给：

```text
bisheng ... -c --asc-aicore-lang <SOURCE>
```

`--asc-aicore-lang` 让编译器按 ASC/CCE 语言解析源文件，因此 `<<<>>>`、`__ccu_host__` 和 CCU 相关扩展可以进入 CCE 前端流程。

### 4.2 CCE AST 和 CodeGen

bisheng 内部为 CCE Kernel Call 提供专用 AST 和 CodeGen 处理。工具中可观察到的相关组件包括：

- `CCEKernelCallExpr`：表示 CCE Kernel 调用表达式；
- `ActOnCCEKernelLaunchCpuMode`：处理 Host/CPU 模式的 launch 语义；
- `emitCCEKernelCallExpr`：生成调用表达式的 Host 侧代码；
- `CGCCERuntime`：生成 CCE Kernel Stub、参数打包和运行时调用。

这些名称用于解释编译器内部职责，不代表 asc-comm 的公开 C API。编译器会把用户写的参数列表整理为一个 Host 可调用的参数包，并把 Kernel 函数地址、launch 配置和参数包交给运行时。

### 4.3 `-fcce-kernel-launch-custom` 的边界

bisheng 帮助信息将 `-fcce-kernel-launch-custom` 描述为：

```text
Transform the kernel launch expression to a user-defined host function.
```

legacy Host 构建模块在 [host_project/CMakeLists.txt](../../../../asc-devkit/cmake/asc/legacy_modules/host_project/CMakeLists.txt:38) 中明确设置了：

```text
--cce-host-only
-fcce-kernel-launch-custom
```

因此可以确认：CCE Host-only 编译支持把 launch 表达式转换为自定义 Host 入口。但不同 asc-devkit CMake 模块可能通过不同的接口库和选项组合完成同一件事；不能假设所有现代样例的最终命令行都显式包含 `-fcce-kernel-launch-custom`。判断某个构建是否启用该模式，应以 `cmake --build <build-dir> --verbose` 打印出的 bisheng 命令为准。

### 4.4 不应依赖生成 Stub 的具体名称

`<<<>>>` 的展开结果可能包含：

1. 参数打包结构；
2. 一个接收 `void*` 参数包的 Host 函数；
3. 对 asc-comm Host launch API 的调用；
4. 必要的返回值和错误码转换。

这些符号名、参数包布局和调用约定由 bisheng 版本决定。asc-comm 只应稳定提供 [ccu_host_launch.h](../../../include/ccu/ccu_host_launch.h:54) 声明的配置结构和 Host launch 函数，不应在业务代码中手写编译器生成的 Stub。

## 5. asc-comm Host Runtime

### 5.1 公开入口和配置结构

Host 入口位于 [ccu_host_launch.cpp](../../../src/ccu/ccu_host_launch.cpp:278)：

```cpp
extern "C" ccu_result asccomm_ccu_host_kernel_launch(
    const void* kernel_func, const asccomm_launch_kernel_cfg* cfg, void* args);
```

配置结构在 [ccu_host_launch.h](../../../include/ccu/ccu_host_launch.h:54) 定义：

```cpp
typedef struct {
    uint32_t num_blocks;
    uint32_t reserved;
    uint64_t phy_die_mask;
    uint64_t binary_cache_tag;
} asccomm_ccu_schd;

typedef struct {
    asccomm_ccu_schd ccu_schd;
    CcuInsHandle ccu_ins;
    aclrtStream stream;
    void* attrs;
} asccomm_launch_kernel_cfg;
```

`HcommCcuHostKernelLaunch` 是兼容 Hcomm 命名和字段命名的适配入口，它把 `HcommLaunchKernelCfg` 转换为 `asccomm_launch_kernel_cfg`，随后进入同一实现，见 [ccu_host_launch.cpp](../../../src/ccu/ccu_host_launch.cpp:307)。

### 5.2 入口执行顺序

`asccomm_ccu_host_kernel_launch` 当前按以下顺序工作：

1. 检查 `kernel_func`、`cfg` 和 `args` 非空；
2. 检查 `num_blocks == 1`；
3. 检查 `phy_die_mask` 只能表示一个 Die，即 `0x01` 或 `0x02`；
4. 检查 Hcomm 注册相关弱符号是否可用；
5. 根据 `binary_cache_tag` 查找或注册 Kernel Handle；
6. 从 Kernel Handle 获取所需任务参数数量；
7. 调用 `CcuKernel::GeneTaskParams` 生成任务描述；
8. 将单个任务描述转换为 `rtCcuTaskInfo_t`；
9. 调用 `rtCCULaunch` 把任务插入指定 Stream。

对应实现分别位于 [ValidateLaunchCfg](../../../src/ccu/ccu_host_launch.cpp:94)、[KernelHandleCache](../../../src/ccu/ccu_host_launch.cpp:142)、[GetSingleTaskParam](../../../src/ccu/ccu_host_launch.cpp:206) 和 [LaunchSingleCcuTask](../../../src/ccu/ccu_host_launch.cpp:244)。

### 5.3 Trampoline：连接两种函数 ABI

Hcomm 当前的注册接口只接受两种函数形式：无参函数，或接收一个 `CcuKernelArg` 的函数。编译器生成的 `__ccu_host__` Host 入口则可能表现为“一个 `void*` 参数包”，再由入口内部解包成用户声明的多个参数。

asc-comm 因此定义了：

```cpp
using VoidPackedCcuKernel = void (*)(void*);

struct VoidKernelRegisterCtx {
    const void* kernelFunc;
    void* packedArgs;
};
```

注册时，`RegisterCcuKernel` 把 `VoidKernelTrampoline` 作为 Hcomm Kernel 函数，并把 `VoidKernelRegisterCtx` 的地址作为唯一 `CcuKernelArg`。trampoline 再执行：

```text
Hcomm -> VoidKernelTrampoline(CcuKernelArg)
      -> 取出 kernelFunc 和 packedArgs
      -> reinterpret_cast<void (*)(void*)>(kernelFunc)
      -> kernelFunc(packedArgs)
```

实现见 [RegisterCcuKernel](../../../src/ccu/ccu_host_launch.cpp:114) 和 [VoidKernelTrampoline](../../../src/ccu/ccu_host_launch.cpp:194)。该上下文只需在同步注册过程内有效；注册完成后，已翻译的 Kernel 不再依赖这个栈上 `registerCtx`。

### 5.4 注册缓存

`binary_cache_tag == 0` 时，每次 launch 都会重新注册并翻译 Kernel。非零时，Host Runtime 使用进程内静态 `KernelHandleCache`：

```text
binary_cache_tag
    -> CcuKernelHandle
    -> HcommCcuGetTaskArgsNum
    -> 复用已翻译 Kernel
```

缓存访问由互斥锁保护，但当前键只有 `binary_cache_tag`，没有包含设备、CCU Instance、Die、资源世代或 Kernel 函数地址。因此调用方必须保证同一 tag 始终代表同一资源上下文和同一 Kernel。通信域销毁、Instance 重建或资源拓扑变化后继续复用旧 tag，可能得到失效或错误的 Kernel Handle；这是当前实现的维护风险。

`asccomm_ccu_get_launch_hash_tag` 使用 XXH3-64 将字符串转换为 tag，见 [ccu_host_launch.cpp](../../../src/ccu/ccu_host_launch.cpp:270)。哈希函数只负责生成标识，不负责验证两个 Kernel 的资源兼容性。

## 6. Hcomm 注册阶段：执行函数体而不是执行硬件

### 6.1 注册事务

Hcomm 的注册流程由 [ccu_launch.cc](../../../../hcomm/src/base_comm/primitives/api_c_adpt/ccu/ccu_launch.cc:44) 实现：

```text
HcommCcuKernelRegisterStart(insHandle)
    -> CcuInstance::BeginRegister()
    -> CcuInstance::Reset()

HcommCcuKernelRegister(...)
    -> CcuKernelMgr::Register(...)
    -> CcuKernelMgr::BuildKernel(...)
    -> 执行 kernelFunc
    -> 保存 CcuKernelHandle

HcommCcuKernelRegisterEnd(insHandle)
    -> CcuInstance::EndRegister()
    -> CcuKernelMgr::Translate(newKernels)
```

如果注册中途失败，Hcomm 会走 `AbortRegister` 或结束注册的回滚路径；调用方不应在注册失败后继续使用未确认有效的 Kernel Handle。

### 6.2 `BuildKernel` 如何解释函数

`CcuKernelMgr::BuildKernel` 在 [ccu_kernel_mgr.cc](../../../../hcomm/src/base_comm/resources/ccu/ccu_kernel/ccu_kernel_mgr.cc:163) 中先创建当前 Kernel，并设置指令生成器、CCU 版本和临时 Die。随后：

- `argNum == 0`：按无参 CCU Kernel 调用；
- `argNum != 0`：当前实现只取 `kernelArgs[0]`，按单个 `CcuKernelArg` 调用；
- 用户 Kernel 函数体中的 `ccu::Write`、`ccu::EventWait`、`ccu::LoadArg` 等 API 只追加 Rep、记录依赖并申请资源；
- 函数体返回后，Hcomm 再处理未闭合控制流、校验 Die、准备常量资源。

也就是说，下面的代码在注册阶段被执行：

```cpp
CCU_CHK_VOID_RET(ccu::LoadArg(ctx.input, argId++));
CCU_CHK_VOID_RET(ccu::NotifyWait(channel, ckeIndex, mask));
CCU_CHK_VOID_RET(ccu::Write(channel, dst, src, size, event, mask));
```

但这些调用不会在这一时刻直接向硬件发送通信请求。它们构造的是稍后由 `Translate` 翻译的 CCU 表示（Rep）和资源依赖。

### 6.3 `RegisterEnd` 的翻译时机

`HcommCcuKernelRegisterEnd` 完成注册事务后，取出尚未翻译的 Kernel，调用 `CcuKernelMgr::Translate`。翻译会生成 mission 指令并固定其起始指令编号、指令数、mission key、资源映射等信息。后续 launch 只需要填充动态参数并引用这些已翻译指令。

因此，CCU 直调通常包含两个逻辑阶段：

```text
一次性或低频：注册 -> 执行 Host 算法描述 -> 翻译并保存 mission
每次调用：    准备 taskArgs -> 生成 SQE 参数 -> rtCCULaunch
```

## 7. 动态参数：`LoadArg` 与 `taskArgs`

### 7.1 参数槽位的建立

Kernel 中每次 `ccu::LoadArg(variable, argId)` 都建立一个动态参数槽位。Hcomm 的 [CcuKernel::LoadArg](../../../../hcomm/src/base_comm/resources/ccu/ccu_kernel/ccu_kernel.cc:857) 会：

1. 将 `argId` 记录到 `loadArgUsedSet_`；
2. 找到对应的 CCU Variable；
3. 创建 `CcuRepLoadArg`；
4. 将该 Rep 追加到指令序列。

注册完成后，`HcommCcuGetTaskArgsNum` 查询 `maxTaskArgsNum`。该值是最大 `argId + 1`，如果没有 `LoadArg` 则为 0，见 [GetCcuKernelInfo](../../../../hcomm/src/base_comm/resources/ccu/ccu_kernel/ccu_kernel.cc:869)。

### 7.2 参数顺序必须一致

`taskArgs[i]` 的含义由 Kernel 中 `LoadArg(..., i)` 决定，而不是由 C++ 形参名称决定。样例的顺序是：

| `argId` | Kernel 中加载的值 | 样例 Host 值 |
| ---: | --- | --- |
| 0 | 输入基地址 | `baseInputAddr` |
| 1 | 本 rank 输出地址 | `baseOutputAddr` |
| 2 | 内存 token | `token` |
| 3 | 输入切片偏移 | `0` |
| 4 | 输出切片偏移 | `inputSize * rankId` |
| 5 | 切片大小 | `inputSize` |
| 6 | 循环地址偏移 | `goSize[0]` |
| 7 | 循环参数 | `goSize[1]` |
| 8 | 并行参数 | `goSize[2]` |
| 9 | 剩余量 | `goSize[3]` |

对应的 `LoadArg` 顺序见 [main.asc](../../../examples/ccu/ccu_direct/01_allgather/main.asc:599)，Host 数组见 [main.asc](../../../examples/ccu/ccu_direct/01_allgather/main.asc:692)。

### 7.3 参数校验

`CcuKernel::ValidateTaskArgs` 要求：

- `argsNum` 必须等于 Kernel 实际使用的 `LoadArg` 数量；
- `argId` 必须从 0 连续出现到 `argsNum - 1`；
- `argsNum > 0` 时 `taskArgs` 不能为空；
- Kernel 必须已经生成非空 mission 指令。

这能捕获漏传参数、重复或跳号 `argId`、注册未完成等错误。错误发生在 [ccu_kernel.cc](../../../../hcomm/src/base_comm/resources/ccu/ccu_kernel/ccu_kernel.cc:261)。

### 7.4 SQE 参数数量和单任务限制

CCU SQE 的参数数组长度为 13。Hcomm 的 `GeneTaskParams` 在参数超过 13 时会把参数拆分到多个 `CcuTaskParam`，每个任务最多携带 13 个参数，见 [ccu_kernel.cc](../../../../hcomm/src/base_comm/resources/ccu/ccu_kernel/ccu_kernel.cc:312)。

但是，当前 asc-comm Host Runtime 在 [GetSingleTaskParam](../../../src/ccu/ccu_host_launch.cpp:206) 中主动限制：

- `argNum > 13` 返回 `CCU_E_NOT_SUPPORT`；
- 生成的 `taskParams` 数量必须恰好为 1，否则返回 `CCU_E_NOT_SUPPORT`。

所以“底层 Hcomm 能拆分多个 SQE 参数任务”不等于“当前 `<<<>>>` 适配入口支持超过 13 个动态参数”。当前样例的 10 个动态参数在限制以内。

## 8. 从任务参数到 `rtCCULaunch`

`CcuKernel::FillTaskParam` 将已翻译 Kernel 的静态信息和本次 Host 参数组合成 `CcuTaskParam`：

```text
静态信息：dieId、missionId、instStartId、instCnt、key、argSize
动态信息：args[0..12]
```

asc-comm 随后构造 `rtCcuTaskInfo_t`，复制这些字段，并设置：

```cpp
taskInfo.timeout = 27U * 68U; // 1836
```

最后调用：

```cpp
rtCCULaunch(&taskInfo, cfg->stream);
```

实现见 [LaunchSingleCcuTask](../../../src/ccu/ccu_host_launch.cpp:244)。Runtime 返回非 `RT_ERROR_NONE` 时，asc-comm 映射为 `CCU_E_RUNTIME`。

需要注意两个行为差异：

1. 当前 asc-comm 直调入口的 timeout 是固定值 1836；
2. Hcomm 的标准 `HcommCcuKernelLaunch` 路径会从运行时配置读取 timeout，并支持多个 `CcuTaskParam`、profiling 和任务 DFX 回调，见 [ccu_launch.cc](../../../../hcomm/src/base_comm/primitives/api_c_adpt/ccu/ccu_launch.cc:266)。

## 9. 静态资源与动态参数的边界

理解两类数据的边界是排查问题的关键：

```text
注册期 kernelArg / channel / Variable / Event
    -> 决定资源拓扑、变量地址、通信指令和 mission

提交期 taskArgs[0..N-1]
    -> 只更新已由 LoadArg 标记的变量值
```

例如样例中的 `kernelArg->channels[i]` 在注册时用于查询远端输出变量和 token 资源，并生成 `WriteVariableWithNotify`、`NotifyWait` 等指令。`baseInputAddr`、`inputSize` 和偏移则通过 `LoadArg` 留出动态槽位，每次调用可以改变。

使用 `binary_cache_tag` 后，缓存命中会跳过注册函数体执行，只查询已有 Kernel 的参数数量。因此如果通道列表、rank 拓扑、Instance 或资源分配发生变化，不能继续使用原 tag 盲目复用。

## 10. 与普通 AICore/AIV launch 的区别

两条路径的共同点是都可以使用 `<<<>>>` 表面语法，但语义完全不同：

| 对比项 | CCU 直调 | 普通 AICore/AIV launch |
| --- | --- | --- |
| Kernel 函数体执行位置 | Host 注册阶段执行，用于生成 CCU Rep | 设备侧 Kernel 执行计算或通信代码 |
| 编译器 lowering | CCE Kernel Call + CCU Host 适配 | 常规 `aclrtlaunch_*` Host Stub |
| 注册/翻译 | Hcomm `CcuKernelMgr` 注册并翻译为 mission | 通常由编译产物和 Runtime 管理设备 Kernel |
| 动态参数 | `LoadArg` 槽位映射到 CCU SQE 参数 | 普通 Kernel 参数打包到设备 Kernel 参数区 |
| 最终 Runtime API | `rtCCULaunch` | `rtKernelLaunch`、`LaunchAscendKernel` 或 `AscendLaunchKernelWithHostArgs` 等 |
| 资源模型 | CCU channel、Variable、Event、mission | AICore/AIV grid、设备代码和普通 stream 任务 |

CCU 路径的核心是“先描述、后翻译、再提交”；不能把 `__ccu_host__` 函数当作普通 AICore Kernel 直接理解。

## 11. 当前实现限制和风险

以下限制均来自当前 asc-comm Host Runtime 或 Hcomm 内部实现：

1. `schd.num_blocks` 只能为 1，虽然字段注释使用了 mission 个数的表述；
2. `phy_die_mask` 只能是单 Die 的 `0x01` 或 `0x02`；
3. 当前 `<<<>>>` 入口最多支持 13 个动态任务参数，且只能生成一个 `CcuTaskParam`；
4. `attrs` 会从 Hcomm 适配结构复制到 asc-comm 配置，但当前实现没有消费它；
5. `reserved` 字段当前未参与校验或调度；
6. timeout 固定为 1836，不跟随 Hcomm 标准 launch 的环境配置；
7. 注册接口通过弱符号连接，四个符号缺失时返回 `CCU_E_NOT_SUPPORT`；
8. 缓存键只有 `binary_cache_tag`，跨设备、Instance 或资源世代复用存在风险；
9. Hcomm 注册 ABI 当前只接受 0 个或 1 个 `CcuKernelArg`，trampoline 是必要的兼容层；
10. `args` 在 asc-comm 顶层入口必须非空，即使某个 Kernel 理论上不需要动态参数。

这些限制意味着当前接口适合样例中的单 mission、单 Die、少量动态参数场景。需要多 mission、多任务参数分片、动态 profiling 或复杂上下文绑定时，应优先复用 Hcomm 的标准 launch 流程，或先扩展 asc-comm 适配层。

## 12. 调试和验证建议

### 12.1 确认编译器路径

使用 verbose 构建检查实际 bisheng 命令：

```bash
cmake --build <build-dir> --verbose
```

重点查看：

- 是否使用 `bisheng`；
- 是否包含 `--asc-aicore-lang`；
- Host-only 构建是否包含 `--cce-host-only`；
- 当前构建模块是否显式加入 `-fcce-kernel-launch-custom`；
- 头文件搜索路径是否包含 `ccu_host_launch.h` 和 Hcomm CCU 头文件。

不要仅凭某个 CMake 模块的选项推断所有样例的实际 lowering；以 verbose 输出为准。

### 12.2 按阶段定位错误

| 现象 | 优先检查 |
| --- | --- |
| 编译器无法识别 `<<<>>>` 或 `__ccu_host__` | bisheng、`--asc-aicore-lang`、Host-only 选项和头文件路径 |
| 返回 `CCU_E_NOT_SUPPORT` | 弱符号是否链接、动态参数是否超过 13、是否生成多个任务 |
| 返回 `CCU_E_PARA` | `num_blocks`、`phy_die_mask`、Kernel Handle 和 Stream |
| 返回 `CCU_E_INTERNAL` | `LoadArg` 数量、argId 连续性、Kernel 是否完成注册和翻译 |
| 注册成功但任务结果错误 | `taskArgs` 顺序、token、地址偏移、缓存 tag 是否复用了错误资源 |
| Runtime 提交失败 | `rtCcuTaskInfo_t` 字段、目标 Die、mission 指令范围和 Stream |

### 12.3 建议关注的日志

标准 Hcomm 路径会输出以下类型的日志，可用于确认阶段边界：

- `HcommCcuKernelRegisterStart` / `HcommCcuKernelRegisterEnd`：注册事务；
- `GeneTaskParam`：任务参数的 Die、mission、指令起点和指令数；
- `HcommCcuKernelLaunch`：Kernel Handle 和 launch 次数；
- `rtCCULaunch` 返回值：Runtime 提交结果。

单元测试 [test_ccu_host_launch.cpp](../../../tests/ut/ccu/test_ccu_host_launch.cpp:84) 已覆盖空指针、非法 mission/Die 配置等基础错误路径。修改 Host launch 逻辑时，应同步补充缓存命中、参数数量和 Runtime 错误映射测试。

## 13. 相关源码索引

| 模块 | 文件 | 重点 |
| --- | --- | --- |
| ASC 样例 | [examples/ccu/ccu_direct/01_allgather/main.asc](../../../examples/ccu/ccu_direct/01_allgather/main.asc:574) | `__ccu_host__` 声明、`LoadArg` 和 `<<<>>>` 调用 |
| asc-comm API | [include/ccu/ccu_host_launch.h](../../../include/ccu/ccu_host_launch.h:54) | launch 配置结构和 C API |
| asc-comm Host Runtime | [src/ccu/ccu_host_launch.cpp](../../../src/ccu/ccu_host_launch.cpp:78) | 校验、trampoline、缓存、任务提交 |
| Hcomm 注册 | [hcomm/.../ccu_launch.cc](../../../../hcomm/src/base_comm/primitives/api_c_adpt/ccu/ccu_launch.cc:44) | RegisterStart/Register/RegisterEnd |
| Hcomm Kernel 构建 | [hcomm/.../ccu_kernel_mgr.cc](../../../../hcomm/src/base_comm/resources/ccu/ccu_kernel/ccu_kernel_mgr.cc:163) | 执行 Host 函数体并生成 Rep |
| 动态参数 | [hcomm/.../ccu_kernel.cc](../../../../hcomm/src/base_comm/resources/ccu/ccu_kernel/ccu_kernel.cc:261) | `LoadArg`、参数校验和 `GeneTaskParams` |
| ASC 编译规则 | [asc-devkit/.../CMakeASCInformation.cmake](../../../../asc-devkit/cmake/asc/asc_modules/CMakeASCInformation.cmake:121) | `bisheng --asc-aicore-lang` |
| Legacy Host 规则 | [asc-devkit/.../host_project/CMakeLists.txt](../../../../asc-devkit/cmake/asc/legacy_modules/host_project/CMakeLists.txt:38) | `--cce-host-only` 和 custom launch 选项 |

## 14. 维护说明

本文档应与以下接口行为一起维护：

- `asccomm_launch_kernel_cfg` 字段和校验规则变化时，更新第 5、11 节；
- Hcomm 注册 ABI 或 `CcuKernelMgr::BuildKernel` 支持的参数形式变化时，更新第 5.3、6.2 节；
- SQE 参数上限、任务分片或 `GeneTaskParams` 语义变化时，更新第 7、8 节；
- 编译器 lowering 选项或 CMake 模块变化时，重新执行 verbose 构建并更新第 4、12 节；
- 缓存键、资源生命周期或 Instance 管理变化时，更新第 5.4、9、11 节。

当前文档不修改 `src/ccu/ccu_host_launch.cpp` 的实现；工作区中该文件已有的用户改动应独立审查和提交。
