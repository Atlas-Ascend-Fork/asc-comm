# 直驱模式通信模型

集合通信的基础是四个**原语概念**——构成所有通信操作的基本元素：

| 概念 | 一句话解释 | 主要对应硬件 |
|------|-----------|---------|
| **通信设备(Endpoint)** | 网络通信的逻辑接口，包含协议与地址 | NPU 网口 / Host NIC |
| **通信通道(Channel)** | 两端通信设备间的数据通道（含同步 Notify） | RoCE QP / UB Jetty 连接 |
| **通信内存(CommMem)** | 注册到通信域、可被通信设备(Endpoint)访问的内存段 | NPU HBM / Host 内存 |
| **通信引擎(CommEngine)** | 执行通信任务的模块，含 Thread 与线程调度器，驱动通信硬件搬移数据 | AICPU_TS、CCU、AIV |


通信过程可分为控制面通信资源创建与数据面通信任务执行两个阶段。在下方通信模型示意图中，黑色实线对应控制面资源创建绑定过程，棕色实线对应数据面任务下发。

![通信模型](../figures/comm_model.svg)

### 控制面：创建通信资源

1. 创建 Endpoint（`HcommEndpointCreate`），指定通信协议、地址与位置信息。
2. 将 CommMem 注册到 Endpoint（`HcommMemReg`），使内存可被远端访问。
3. 基于 Endpoint 创建 Channel（`HcommChannelCreate`），传入 `EndpointHandle` 完成资源绑定；同时本端与远端交换注册内存信息。

### 数据面：通信引擎下发通信任务驱动硬件执行

通信引擎执行用户下发的指令，将要执行的通信任务描述记录在Channel中，包括通信内存地址、通信操作等信息，根据不同协议驱动硬件执行。