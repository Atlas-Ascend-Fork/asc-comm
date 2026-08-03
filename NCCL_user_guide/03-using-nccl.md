# 03 · Using NCCL

> 原文：Using NCCL

## 接入步骤

用 NCCL 和用任何其他库没什么区别：

1. 在系统上安装 NCCL 库
2. 修改应用，链接该库
3. 在应用中 include 头文件 `nccl.h`
4. 创建一个 communicator（见 `04-creating-a-communicator.md`）
5. 用 NCCL 集合通信原语做数据通信

## 核心概念

集合通信原语是**一组 CUDA 设备之间常见的数据传输模式**。一个通信算法涉及多个共同参与通信的处理器。

| 概念 | 含义 |
| --- | --- |
| **rank** | 每个 CUDA 设备在通信组内由一个从 0 开始的索引（rank）标识 |
| **communicator** | 每个 rank 用一个 communicator 对象来指代那组打算协同工作的 GPU |

**创建 communicator 是发起任何通信操作之前的第一步。**

## 本章包含的子章节

原文 "Using NCCL" 是一个总览页，下挂以下子章节（本目录中对应文件已列出）：

### Creating a Communicator → `04`
- 带 options 创建 communicator
- 用多个 `ncclUniqueId` 创建 communicator
- 收缩（shrink）communicator
- 扩展（grow）communicator
- 创建更多 communicator
- 并发使用多个 NCCL communicator
- Finalize communicator
- Destroy communicator
- 错误处理与 communicator abort
  - 异步错误与错误处理
  - 容错（Fault Tolerance）
- Quality of Service

### Collective Operations → `05`
AllReduce / Broadcast / Reduce / AllGather / ReduceScatter / AlltoAll / Gather / Scatter

### Data Pointers → `06`

### CUDA Stream Semantics → `07`
- 在同一个 `ncclGroupStart/End()` group 内混用多个 stream

### Group Calls → `08`
- 从一个线程管理多个 GPU
- 聚合操作（Aggregated Operations，2.2 及以后）
- Group 操作的定序语义
- 非阻塞 group 操作

### Point-to-point communication → `09`
- 双边（two-sided）通信
- 单边（one-sided）通信

### Thread Safety → `10`

### In-place Operations → `11`

### Using NCCL with CUDA Graphs → `12`
- 要求与限制

### User Buffer Registration → `13`
- NVLink SHARP buffer 注册
- IB SHARP buffer 注册
- 通用 buffer 注册
- Buffer 注册、GPU Direct RDMA 与 MPS（MLOPart）
- Buffer 注册与 PXN
- Memory Allocator
- Window 注册
- Zero-CTA 优化

### Device-Initiated Communication → `14`
- Device API
- 要求
- 跨版本兼容性
- Host 侧 setup
- 简单 LSA kernel
- Multimem device kernel
- Thread groups
- Teams
- Segment types
- Host 可访问的 device 指针函数
- GIN device kernel
