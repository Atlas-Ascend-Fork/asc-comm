# 05 · Collective Operations

> 原文：Using NCCL → Collective Operations

## 通用前提

集合操作必须**由每个 rank（也即每个 CUDA 设备）都调用，且使用相同的 count 和相同的 datatype**，才构成一次完整的集合操作。

做不到这一点的后果是未定义行为，包括 hang、crash 或数据损坏。

## AllReduce

对各设备上的数据做归约（求和、min、max 等），并把结果存到**每个 rank** 的接收 buffer 中。

k 个 rank 之间的 sum allreduce：每个 rank 提供一个含 N 个值的数组 `in`，收到相同的含 N 个值的结果数组 `out`，其中

```
out[i] = in0[i] + in1[i] + … + in(k-1)[i]
```

对应 API：`ncclAllReduce()`

## Broadcast

把 root rank 上的 N 元素 buffer 复制到所有 rank。

**重要**：`root` 参数是一个 **rank，不是设备号**，因此会受 rank 到设备映射方式的影响。

对应 API：`ncclBroadcast()`

## Reduce

与 AllReduce 做相同的归约，但结果**只存到指定 root rank** 的接收 buffer 中。

**重要**：`root` 参数是 rank 而非设备号，受 rank 到设备映射影响。

**等价关系**：Reduce 之后接一个 Broadcast，等价于 AllReduce。

对应 API：`ncclReduce()`

## AllGather

从 k 个 rank 各收集 N 个值，汇聚成大小为 `k*N` 的输出 buffer，并把该结果分发给所有 rank。

**输出按 rank 索引排序**，因此 AllGather 受 rank 到设备映射的影响。

**等价关系**：ReduceScatter 之后接 AllGather，等价于 AllReduce。

对应 API：`ncclAllGather()`

## ReduceScatter

与 Reduce 做相同的归约，但结果被**等大小分块 scatter 到各 rank**，每个 rank 按自己的 rank 索引拿到一块数据。

因为 rank 决定了数据布局，ReduceScatter 受 rank 到设备映射的影响。

对应 API：`ncclReduceScatter()`

## AlltoAll

k 个 rank 之间：每个 rank 提供大小为 `k*N` 的输入 buffer，其中第 j 块（N 个值）发给目标 rank j；每个 rank 收到大小为 `k*N` 的输出 buffer，其中第 i 块（N 个值）来自源 rank i。

即所有 rank 之间交换数据，每个 rank 给其他每个 rank 发不同的数据、也从其他每个 rank 收不同的数据。

对应 API：`ncclAlltoAll()`

## Gather

从 k 个 rank 各收集 N 个值，汇聚到 **root rank 上**大小为 `k*N` 的输出 buffer。

**重要**：`root` 是 rank 而非设备号，受 rank 到设备映射影响。

对应 API：`ncclGather()`

## Scatter

把 root rank 上总共 `N*k` 个值分发到 k 个 rank，每个 rank 收到 N 个值。

**重要**：`root` 是 rank 而非设备号，受 rank 到设备映射影响。

对应 API：`ncclScatter()`

## 速查

| 操作 | 输入/rank | 输出/rank | 结果落在 |
| --- | --- | --- | --- |
| AllReduce | N | N | 所有 rank |
| Broadcast | N（root） | N | 所有 rank |
| Reduce | N | N | 仅 root |
| AllGather | N | k*N | 所有 rank |
| ReduceScatter | k*N | N | 所有 rank（各一块） |
| AlltoAll | k*N | k*N | 所有 rank |
| Gather | N | k*N（root） | 仅 root |
| Scatter | N*k（root） | N | 所有 rank |

带 root 的操作（Broadcast / Reduce / Gather / Scatter）以及所有按 rank 索引决定数据布局的操作（AllGather / ReduceScatter / AlltoAll），都对 rank↔设备映射敏感。
