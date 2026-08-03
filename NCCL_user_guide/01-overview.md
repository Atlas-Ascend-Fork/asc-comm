# 01 · NCCL 概述

> 原文：Overview of NCCL

## 是什么

NCCL（读作 "Nickel"）是 NVIDIA 的集合通信库，提供**拓扑感知**的 GPU 间通信原语，易于集成到应用中。

定位要点：**它不是一个完整的并行编程框架**，而是一个专注于加速 GPU 间通信的库。

## 提供的原语

集合通信原语：

- AllReduce
- Broadcast
- Reduce
- AllGather
- ReduceScatter
- AlltoAll
- Gather
- Scatter

此外还提供点对点 send/receive，可用于自行构造 scatter、gather、all-to-all 等操作。

## 实现上的关键设计

**单 kernel 内完成通信 + 计算。** 集合通信的核心难点是参与方之间的紧密同步。传统 CUDA 实现方式是「CUDA memcpy + 做本地归约的 CUDA kernel」组合。NCCL 不同：每个集合操作都在**单个 kernel** 里同时处理通信和计算。好处是同步快，且达到峰值带宽所需的资源更少。

**免去按机器手工调优。** NCCL 在单节点内和跨节点的多 GPU 上都提供高速集合通信，支持多种互连：PCIe、NVLink、InfiniBand Verbs、IP sockets。开发者不需要针对特定机器去优化应用。

## API 设计

除性能之外，易用性是设计的首要考虑：

- 简单的 **C API**，方便从各种语言调用。
- 紧贴 **MPI** 定义的那套广为人知的集合通信 API，熟悉 MPI 的人会觉得 NCCL 的 API 很自然。
- 与 MPI 的一处小差异：**NCCL 的集合操作带一个 stream 参数**，从而直接融入 CUDA 编程模型。

## 兼容的并行化模型

NCCL 基本兼容任何多 GPU 并行化模型：

- 单线程控制所有 GPU
- 多线程，例如每 GPU 一个线程
- 多进程，例如配合 MPI

## 典型应用

在深度学习框架中应用广泛，神经网络训练大量使用 AllReduce。依靠 NCCL 提供的多 GPU、多节点通信能力，神经网络训练可以高效扩展。
