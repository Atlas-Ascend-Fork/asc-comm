# AICPU Asccomm接口简介

AICPU Asccomm提供面向通信线程和通信通道的数据面C接口，用于描述本地内存操作、跨通道数据搬运、通知同步以及批量提交操作。安装后的公开头文件路径为`"aicpu/hcomm/asccomm_primitives.h"`。`AsccommDataType`、`AsccommReduceOp`、`AsccommBatchTransferDesc`等本组件类型定义于`"aicpu/hcomm/asccomm_res_defs.h"`；`ThreadHandle`和`ChannelHandle`的基础定义由运行环境的`hcomm_res_defs.h`提供。包含`"aicpu/hcomm/asccomm_primitives.h"`后，上述类型均可直接使用。

## 接口分类

- [数据搬运](./data_movement/README.md)：本地拷贝/归约，以及基于Channel的读、写和归约读写操作。
- [同步](./synchronization/README.md)：Thread Notify、ACL运行时Notify和Channel Notify同步，以及Fence、Drain和超时设置操作。
- [批量操作](./batch_operations/README.md)：批量模式控制和批量传输描述符提交。
