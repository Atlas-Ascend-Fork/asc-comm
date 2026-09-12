# CCU 解耦直调 AllGather 样例

本样例是迁移后 `asc-comm` 的完整 CCU `<<<>>>` 直调流程，使用 AllGather 验证数据面与 Hcomm
控制面的解耦接口。它替代了旧的 `examples/ccu/ccu_direct/*` wrapper 样例。

## 流程

每个可见 NPU 创建一个 HCCL rank，样例依次完成：

1. 初始化 ACL，创建 RootInfo、HCCL 通信域和 Stream。
2. 申请 Device Buffer，并按 rank 初始化输入数据。
3. 从 HCCL rank graph 获取 CCU Channel，并查询通信域持有的 CCU Instance。
4. 通过 `HcclCommQueryCcuRegisterContext` 获取 Register Context，通过
   `HcommCcuGetLaunchContext` 获取当前 Thread/Stream 的 Launch Context。
5. 通过 `asccomm_ccu_host_launch_attrs_init` 构造 attrs，并在当前 Host 线程 bind。
6. 调用 `CcuAllGatherMesh1DMem2MemKernel<<<schd, instanceHandle, stream>>>`。编译器 Host stub
   进入 `asccomm_ccu_host_kernel_launch`，再由数据面完成注册、翻译和 `rtCCULaunch`。
7. 查询 Host launch 错误，同步 Stream，拷回结果并校验每个 rank 的数据段。
8. 停止新 launch，调用 `asccomm_ccu_instance_release` 清理数据面状态，再由通信域销毁其 Instance，
   最后销毁 Stream、Buffer 和 ACL。

`bind`、`<<<>>>` 和 `unbind` 必须在同一个 Host 线程执行；Register Context 和 Launch Context 只在本次
同步调用期间借用。若配套编译器能将 attrs 直接放入 `cfg.attrs`，Host stub 可以跳过显式 bind，数据面会
优先读取 `cfg.attrs`。

## 构建

先构建数据面动态库：

```bash
cmake -S ../../.. -B build-asccomm -DASCCOMM_BUILD_CCU=ON
cmake --build build-asccomm --target asccomm_ccu_dataplane_compile_check -j
```

再构建本样例：

```bash
cmake -S . -B build \
  -DCMAKE_ASC_ARCHITECTURES=dav-3510 \
  -DASCCOMM_LIBRARY_DIR=$(realpath build-asccomm/src/ccu)
cmake --build build -j
./ccu_direct_decoupled_example
```

需要支持 `__ccu_host__` 和 CCU 自定义 launch ABI 的 ASC 编译器，以及提供
`HcommCcuGetLaunchContext` 的配套 Hcomm 控制面。当前 CANN 9.2.0 的 bisheng 不识别
`__ccu_host__`，因此只能完成数据面和头文件验证，不能在该环境真实编译或运行本样例。

运行前需要按目标 CANN 环境设置 `ASCEND_HOME_PATH`，并开启 CCU 调度模式：

```bash
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
```

运行至少需要两张 NPU；单卡仅用于编译/流程检查。
