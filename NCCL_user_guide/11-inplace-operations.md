# 11 · In-place Operations

> 原文：Using NCCL → In-place Operations

## 与 MPI 的差异

MPI 定义了一个特殊的 "in-place" 值来替代指针；**NCCL 不这么做**。NCCL 的做法是：**当传入的指针实际上构成 in-place 时，自动对这种情况做优化**。

也就是说，你不需要传什么特殊标记，只要指针关系符合下面的规则，NCCL 就会走 in-place 路径。

## 各操作的 in-place 判定

### ncclBroadcast / ncclReduce / ncclAllReduce

传入 `sendBuff == recvBuff` 即为 in-place 操作：最终结果存放在与初始数据读取位置相同的地方。

### ncclReduceScatter / ncclAllGather

当**每个 rank 的指针位于全局 buffer 中该 rank 对应的偏移处**时，构成 in-place。

更精确地说，以下调用被视为 in-place：

```c
ncclReduceScatter(data, data+rank*recvcount, recvcount, datatype, op, comm, stream);
ncclAllGather(data+rank*sendcount, data, sendcount, datatype, op, comm, stream);
```

注意两者方向相反：

| 操作 | 大 buffer 在哪一侧 | 带 rank 偏移的在哪一侧 |
| --- | --- | --- |
| `ncclReduceScatter` | send（`data`） | recv（`data + rank*recvcount`） |
| `ncclAllGather` | recv（`data`） | send（`data + rank*sendcount`） |

这符合两个操作本身的语义：ReduceScatter 输入大、输出每 rank 一块；AllGather 输入每 rank 一块、输出大。
