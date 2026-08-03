# 07 · CUDA Stream Semantics

> 原文：Using NCCL → CUDA Stream Semantics

## 基本语义

NCCL 调用关联到一个 stream，它作为集合通信函数的**最后一个参数**传入。

- NCCL 调用在操作**已被有效入队到给定 stream** 时返回，或者返回一个错误。
- 集合操作随后在 CUDA 设备上**异步执行**。
- 操作状态用标准 CUDA 语义查询，例如调 `cudaStreamSynchronize` 或使用 CUDA event。

也就是说：NCCL 调用返回 ≠ 通信完成，只代表入队成功。判断完成要走 CUDA 的那套机制。

## 在同一个 ncclGroupStart/End() group 内混用多个 stream

NCCL 允许在一个 group 调用内使用多个 stream，效果是：

- NCCL kernel 启动**之前**，强制建立对所有这些 stream 的依赖
- NCCL kernel 完成**之前**，阻塞所有这些 stream

行为上就像这个 NCCL group 操作被 post 到了每一个 stream 上。但由于它实际是单个操作，**会在这些 stream 之间造成一个全局同步点**。

实践含义：group 内混 stream 是可行的，但代价是把这些 stream 强行同步到一起，本来想要的并行度会被抹掉。如果不希望产生这个同步点，就不要在同一个 group 里跨 stream。
