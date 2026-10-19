# AICPU Asccomm接口简介

[AICPU](../../../programming_model/abstract_hardware_architecture.md#通信引擎) Asccomm提供面向通信线程和通信通道的数据面C接口，用于描述本地内存操作、跨通道数据搬运、通知同步以及批量提交操作。安装后的公开头文件路径为`"aicpu/hcomm/asccomm_primitives.h"`。`AsccommDataType`、`AsccommReduceOp`、`AsccommBatchTransferDesc`等本组件类型定义于`"aicpu/hcomm/asccomm_res_defs.h"`；`ThreadHandle`和`ChannelHandle`的基础定义由运行环境的`hcomm_res_defs.h`提供。包含`"aicpu/hcomm/asccomm_primitives.h"`后，上述类型均可直接使用。

## 接口分类

- [本地操作](./local_operations/README.md)：Thread上的本地拷贝和本地归约操作。
- [远端操作](./remote_operations/README.md)：基于Channel的读写、归约读写、带通知写操作，以及批量描述符传输操作。
- [同步与屏障](./synchronization_and_barrier/README.md)：Thread Notify和Channel Notify同步，以及Fence、Drain操作。
- [其他](./other_operations/README.md)：批量模式控制、Notify等待默认超时时间设置和Thread资源获取超时时间设置。
