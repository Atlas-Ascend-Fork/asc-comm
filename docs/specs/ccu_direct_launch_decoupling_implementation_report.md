# CCU `<<<>>>` 直调解耦实施报告

## 1. 实施结论

本次修改已在 `ccu_rename/asc-comm` 内补齐 CCU `<<<>>>` Host 适配层，并将直调路径统一接入迁移后的
数据面注册和执行接口：

```text
CCU Kernel<<<schedule, instanceHandle, stream>>>(args...)
    -> asccomm_ccu_host_kernel_launch / HcommCcuHostKernelLaunch
    -> asccomm_ccu_kernel_register_start/register/register_end
    -> asccomm_ccu_get_task_args_num
    -> asccomm_ccu_kernel_launch
    -> rtCCULaunch
```

Host 适配层不再调用旧的 `HcommCcuKernelRegister*`、`HcommCcuGetTaskArgsNum`，也不重复实现 task 参数
生成、profiling、timeout、task report 或 Runtime 提交逻辑。

数据面动态库已构建通过，公开头已通过 C/C++ 语法检查，导出符号及旧依赖检查通过。受当前 ASC 编译器
不识别 `__ccu_host__` 的限制，新增 direct 样例尚未完成真实 `<<<>>>` 编译及 NPU 运行验证。

## 2. 实现思路

### 2.1 上下文传递

新增版本化 POD `AsccommCcuHostLaunchAttrsV1`，集中携带：

- `HcommCcuRegisterContextHandle`；
- 本次同步调用的 `HcommCcuLaunchContextPod`；
- Instance 资源 `generation`；
- 固定 ABI 版本、Magic、结构大小和预留字段。

Host adapter 按以下顺序取得上下文：

1. 优先读取编译器 Host 配置中的 `cfg.attrs`；
2. `cfg.attrs == nullptr` 时，读取调用线程通过 `asccomm_ccu_host_context_bind` 显式绑定的上下文；
3. 两种来源都不存在时返回 `CCU_E_UNAVAIL`。

线程绑定仅对当前 Host 线程生效，不是进程级全局 Instance 表。它用于兼容当前只有
`<<<schedule, instanceHandle, stream>>>` 三个配置参数、无法直接传入第四个上下文参数的 Host ABI。

### 2.2 注册和缓存

缓存键包含：

```text
deviceLogicId + instanceHandle + generation + dieId + kernelFunc + binaryCacheTag
```

因此 Kernel Handle 不会跨设备、Instance、资源世代或 Kernel 函数复用。`binaryCacheTag == 0` 时禁用
缓存并重新注册。Host adapter 使用单一互斥锁串行化完整注册事务，避免两个线程交叉执行
`RegisterStart/Register/RegisterEnd`。

编译器传入的 `void (*)(void*)` 打包 Kernel 通过单参数 trampoline 适配现有数据面
`CcuResult (*)(void*)` 注册 ABI。trampoline 上下文只在同步注册调用期间有效，不保存到 Registry。

### 2.3 生命周期和失败回滚

RegistryMgr 和 KernelMgr 的查询结果由裸指针改为 `shared_ptr`。从 map 删除 Registry/Kernel 后，已取得
引用的 launch、translate 或 DFX 调用仍可安全完成对象访问，不会使用已释放对象。

新增 `asccomm_ccu_instance_release(deviceLogicId, instanceHandle, generation)`，负责：

```text
清理对应 Host cache
    -> 从 RegistryMgr 删除指定 generation 的 Instance
    -> Registry 析构并注销其 Kernel
    -> 释放该 Instance 的 Variable/Event 资源
```

注册轮次增加 owner 线程检查和资源池检查点。只有翻译成功后才清空待翻译列表并提交；注册或翻译失败时，
注销本轮 Kernel 并恢复本轮开始前的资源余量。相同 Instance/generation 的后续注册不再重新加载完整资源
快照，避免已分配资源被错误放回余量池。

## 3. 修改点

| 文件/目录 | 修改内容 |
| --- | --- |
| `include/ccu/hcomm/ccu_host_launch.h` | 新增 Host ABI、版本化 attrs、上下文初始化/绑定、错误查询、兼容入口和 Hash 接口 |
| `include/ccu/hcomm/ccu_launch.h` | 新增按 device、Instance 和 generation 释放数据面状态的接口 |
| `include/hcomm/hcomm_types.h` | 已包含 HCCL 公共类型时复用配套包定义，避免完整样例混合包含时重复定义 ABI 枚举 |
| `src/ccu/hcomm/primitives/ccu_host_launch.cc` | 新增上下文校验、trampoline、注册、缓存、统一 launch 转发、释放实现和 C ABI 异常边界 |
| `src/ccu/hcomm/primitives/ccu_launch.cc` | 注册事务改为翻译后提交；失败回滚；共享持有 Kernel/Registry；timeout 向 `uint16_t` 显式饱和转换 |
| `src/ccu/hcomm/resource/kernel/ccu_kernel_registry*` | 增加注册 owner、资源检查点、同 generation 快照复用、Registry 安全删除 |
| `src/ccu/hcomm/resource/kernel/ccu_kernel_mgr*` | Kernel map 改为 `shared_ptr`；translate/launch/DFX 持有有效引用；配置与翻译互斥 |
| `src/ccu/hcomm/CMakeLists.txt` | 将 Host adapter 编入 `asccomm_ccu_dataplane` |
| `examples/ccu/ccu_direct_decoupled/` | 新增一个完整 AllGather 解耦直调样例，并移除依赖旧 Host wrapper 的 `ccu_direct` 样例 |

原 `ccu_launch.cc` 中已存在的参数名 `asccomm_ccu_kernel_launch` 被保留，并在函数内增加
`launch_context` 局部别名，以兼容函数体和避免覆盖已有修改。

## 4. 新增接口使用方式

当编译器不能设置 `cfg.attrs` 时，调用顺序如下：

```cpp
AsccommCcuHostLaunchAttrsV1 attrs{};
asccomm_ccu_host_launch_attrs_init(registerContext, &launchContext, &attrs);
asccomm_ccu_host_context_bind(&attrs);

Kernel<<<schedule, instanceHandle, stream>>>(args...);
CcuResult ret = asccomm_ccu_host_get_last_error();

asccomm_ccu_host_context_unbind();
```

调用方应保证初始化、bind、`<<<>>>` 和 unbind 在同一个 Host 线程执行。Instance 销毁前，控制面必须先
阻止新调用并等待在途同步 Host launch 返回，然后调用 `asccomm_ccu_instance_release`。

新增样例已将上述链路放入完整的多 rank 流程：ACL/HCCL 初始化、Channel 获取与 Instance 查询、Register/Launch
Context 查询、attrs 绑定、CCU `<<<>>>` 调用、结果校验，以及 Instance、通信域、Stream 和 Device Buffer
清理。样例还显式调用 `asccomm_ccu_get_mem_token`，不依赖旧 Hcomm 数据面 token 接口。

## 5. 验证结果

### 已通过

1. 数据面独立配置与构建：

   ```bash
   cmake -S . -B /tmp/asccomm-direct-decoupled-final \
     -DASCCOMM_BUILD_CCU=ON -DCMAKE_BUILD_TYPE=Release
   cmake --build /tmp/asccomm-direct-decoupled-final \
     --target asccomm_ccu_dataplane_compile_check -j 8
   ```

2. `ccu_host_launch.h` 分别通过 C11、C++17 语法检查。
3. Host API 冒烟验证通过：空指针错误码、线程局部 last error、Hash、非法 release 参数符合预期。
4. 动态库已导出新增接口和两个编译器兼容入口。
5. `nm -D --undefined-only` 未发现以下旧符号：

   ```text
   HcommCcuKernelRegisterStart
   HcommCcuKernelRegister
   HcommCcuKernelRegisterEnd
   HcommCcuGetTaskArgsNum
   HcommCcuKernelLaunch
   ```

### 未通过

新增样例 CMake 配置成功，但当前 CANN 9.2.0 的 `bisheng/ccec` 在编译完整 `main.asc` 时失败：

```text
error: unknown type name '__ccu_host__'
```

因此本环境不能验证编译器生成 Host stub、参数打包、实际 Host 入口选择和 NPU 执行结果。

## 6. 不确定项和遗留问题

1. **编译器 Host ABI 必须最终确认。** 当前实现按原仓接口假设 Host stub 调用
   `asccomm_ccu_host_kernel_launch` 或 `HcommCcuHostKernelLaunch`，且 `kernelFunc` 可按
   `void (*)(void*)` 接收打包参数。必须使用支持 `__ccu_host__` 的配套编译器，通过符号表、反汇编和
   实际运行确认该假设。
2. **Hcomm 控制面需要配套改造。** 当前安装的 `libhcomm.so` 可见 `HcclCommQueryCcuIns` 和旧
   `HcommCcuKernelLaunch`，但没有 `HcclCommQueryCcuRegisterContext`、`HcommCcuGetLaunchContext`。
   Hcomm 必须提供 RegisterContext/LaunchContext，并在销毁路径调用 Instance release。
3. **`cfg.attrs` 是否透传尚未验证。** 若编译器不能设置该字段，应继续使用本次提供的同线程 bind/unbind；
   若 Host stub 会切换线程，该兼容方式不成立，必须扩展编译器 Host ABI 显式传递 opaque context。
4. **Cache tag 语义由调用方保证。** 相同 tag 必须代表相同注册期捕获资源；若捕获参数变化，应使用新 tag
   或将 tag 设为 0。tag 为 0 时产生的 Kernel 会保留到 Instance release，频繁使用可能耗尽资源。
5. **同设备多 CCU Instance 仍需专项验证。** 当前 KernelMgr 的 translator/control-ops 配置是 per-device，
   继承自迁移代码，不是 per-Instance。若一个设备并行使用多个不同资源世代的 Instance，需要进一步把
   translator 配置按 Instance/generation 隔离。
6. **释放并发契约需要控制面保证。** `shared_ptr` 已消除对象 UAF，但控制面仍必须等待在途任务完成后再释放
   Runtime/CCU 资源；本仓无法单独阻止 Hcomm 提前销毁其资源。
7. **硬件回归尚未完成。** 仍需在匹配的软件栈上覆盖单/多 Die、重复缓存命中、注册/翻译失败、profiling、
   report callback、timeout 饱和、多 task 展开和 Instance 重建。

## 7. 后续验收条件

配套编译器和 Hcomm 完成后，应至少满足：

- 新增样例成功编译、链接并在 NPU 上执行；
- Host stub 实际进入本次新增的 adapter；
- 相同 cache tag 重复 launch 命中同一 Kernel Handle；
- Instance 销毁并重建后旧 generation 的缓存和 Handle 不再可用；
- profiling、task report、timeout 和多 task 行为与显式 `asccomm_ccu_kernel_launch` 路径一致。
