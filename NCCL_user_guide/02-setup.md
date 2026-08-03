# 02 · Setup

> 原文：Setup

## NCCL 不负责什么

NCCL 是一个为高性能应用提供优化过的 GPU 到 GPU 通信的通信库。它**不像 MPI 那样提供一个完整的并行环境**——没有进程启动器（process launcher），也没有进程管理器。

因此 NCCL 的 bootstrap 依赖于：

- 应用自己的**进程管理系统**（比如 MPI 的 mpirun、torchrun、Slurm 等）
- 应用自己的 **CPU 侧通信系统**

也就是说，"怎么把这些进程拉起来、怎么让它们互相认识"这件事不在 NCCL 职责范围内，NCCL 只接手 GPU 间的数据搬运。

## 安全性：用户责任

和 MPI 以及其他为性能优化的库一样，**NCCL 不提供 GPU 之间的安全网络通信**（没有加密、没有认证）。

确保 NCCL 运行在安全网络上是**用户的责任**，两个环节都要管：

| 环节 | 说明 |
| --- | --- |
| bootstrap | 由环境变量 `NCCL_SOCKET_IFNAME` 控制走哪个网络接口 |
| 高速通信 | 实际数据传输通道（NVLink / IB / socket） |

实践含义：不要把 NCCL 的 bootstrap 接口暴露在不可信网络上。用 `NCCL_SOCKET_IFNAME` 显式指定内部管理网口，避免 NCCL 自动选到一个对外可达的接口。
