# 23 · API · Device API 总览

> 原文：NCCL API → Device API

Device API 让通信可以**从 device（GPU）代码发起和执行**。入门介绍和使用示例见 `14-device-initiated-communication.md`。

## 四个组成部分

| 部分 | 内容 | 本目录文件 |
| --- | --- | --- |
| **Host-Side Setup** | 创建和配置 device communicator、查询属性、host 可访问的 device 指针函数及相关类型 | `24` |
| **Memory and LSA** | load/store 可访问（LSA）内存、barrier、指针访问器、multimem | `25` |
| **GIN**（GPU-Initiated Networking） | 单边传输、signal、counter、网络 barrier | `26` |
| **Reduce / Broadcast / Fused 构件** | 计算融合 kernel 的构件：reduce、copy（broadcast）、reduce-then-copy，用于实现 AllReduce、AllGather、ReduceScatter 等算法 | `27` |

## 各章节的子结构

### Device API – Host-Side Setup（`24`）
- Host-Side Setup
- Host-Accessible Device Pointer Functions

### Device API – Memory and LSA（`25`）
- LSA
- Multimem

### Device API – GIN（`26`）
- GIN

### Device API – Remote Reduce and Copy（`27`）
- 编译期要求
- API 总览
- ReduceSum —— N 个源到一个目的
- Copy（Broadcast）—— 一个源到 N 个目的
- ReduceSumCopy
- 基于 Lambda 的方式（自定义 layout）
- 自定义归约算子
