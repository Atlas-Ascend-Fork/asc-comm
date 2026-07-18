# CCU UT Interface 适配记录

## 1. 目标

在不修改 `src/` 的前提下，使 `ccu_interface_assist.cc` 与 interface REP UT 可独立构建运行。

## 2. 关键处理

- 将 `AppendToContext` 和 `CreateVariable` 从通用 exception stub 分离到 `ccu_rep_assist_stub.cc` 独立静态库，避免与生产 `ccu_interface_assist.cc` 重定义。
- 新增 `ccu_kernel_stub.cc`，仅提供 `CcuKernel` 析构、`Append` 和 `CreateVariable`，满足 production assist 的 RTTI/vtable/链接需求；不编入完整 kernel 产品实现。
- interface CMake 显式补齐 translator、loop、setloop、loadarg、operator 和 microcode assist 源码闭包，并加入 `${CCU_IMPL_INCLUDE_DIRS}`。

当前 interface 测试只覆盖 `CreateVariable(nullptr)` 的异常路径，不构造或验证真实 kernel 资源行为。

## 3. 验证

```bash
cmake --build build/ut-hcomm --target hccl_utest_comms_ccu_rep_interface -j8
build/ut-hcomm/ccu_hcomm/ccu_representation/interface/hccl_utest_comms_ccu_rep_interface
```

结果：`hccl_utest_comms_ccu_rep_interface` 的 32 项测试全部通过。
