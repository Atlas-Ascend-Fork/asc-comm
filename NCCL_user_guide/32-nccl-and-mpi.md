# 32 · NCCL 与 MPI

> 原文：NCCL and MPI

## API 层面的差异

NCCL 的 API 和用法与 MPI 相似，但有不少细微差别。

### 每进程多设备

类似 MPI endpoints 的概念，**NCCL 不要求 rank 与进程 1:1 映射**。一个 NCCL communicator 可以把多个 rank（因而多个设备）关联到单个进程。因此与 MPI 配合时，**单个 MPI rank（即一个 NCCL 进程）可以关联多个设备**。

### ReduceScatter 对应哪个 MPI 操作

`ncclReduceScatter` 对应的是 **`MPI_Reduce_scatter_block`**，**不是** `MPI_Reduce_scatter`。

原因：`MPI_Reduce_scatter` 本质上是一个"向量"函数（每 rank 可以不同 count），而 `MPI_Reduce_scatter_block`（后来定义以补齐缺失语义）提供规整的 count，与其镜像函数 `MPI_Allgather` 一致。这是 MPI 出于合理的向后兼容原因未修正的一处怪异之处，**NCCL 不沿用它**。

### 发送与接收 count

MPI 在许多集合操作中允许发送和接收的 count 与类型不同，只要 `sendcount*sizeof(sendtype) == recvcount*sizeof(recvtype)`。**NCCL 不允许**，只定义单一 count 和单一数据类型。

对 AllGather 和 ReduceScatter，**count 等于 per-rank 的大小，也就是较小的那个**；另一侧的 count 等于 `nranks*count`。函数原型清楚地表明提供的是哪个 count：

| 函数 | 参数中的 count |
| --- | --- |
| `ncclAllGather` | `sendcount` |
| `ncclReduceScatter` | `recvcount` |

**注意**：用 ReduceScatter + AllGather 组合来执行或对比 AllReduce 时，把 `sendcount` 和 `recvcount` 定义为总 count 除以 rank 数；若不能整除，**要正确地向上取整**。

### 其他集合操作与点对点操作

NCCL **没有**为以下操作定义专门的接口：sendrecv、gather、gatherv、scatter、scatterv、alltoall、alltoallv、alltoallw，以及 neighbor collectives。

所有这些操作都可以用 `ncclSend` + `ncclRecv` + `ncclGroupStart`/`ncclGroupEnd` 的组合简单表达，方式类似于用 `MPI_Isend` + `MPI_Irecv` + `MPI_Waitall` 表达（具体模式见 `09-point-to-point.md`）。

`ncclRecv` 的三个限制：

- **不支持 `MPI_ANY_SOURCE` 的等价物**，必须始终提供具体的源 rank。
- 提供的接收 count **必须与发送 count 匹配**。
- **没有 message tag 的概念。**

### In-place 操作

见 `11-inplace-operations.md`。

## 在 MPI 程序中使用 NCCL

NCCL 很容易与 MPI 配合使用。NCCL 集合操作与 MPI 集合操作相似，因此**从一个 MPI communicator 创建 NCCL communicator 很直接**，从而可以方便地用 MPI 做 CPU 到 CPU 的通信、用 NCCL 做 GPU 到 GPU 的通信。

但 MPI 的某些实现细节在 NCCL 与 MPI 混用时会引发问题。

### MPI Progress 导致的死锁

MPI 定义了 **progress** 的概念：MPI 操作需要程序调用 MPI 函数（可能多次）才能推进并最终完成。

**在某些实现中，一个 rank 上的 progress 可能需要另一个 rank 调用 MPI。** 虽然这通常对性能不利，但可以认为它是合法的 MPI 实现。

后果：**阻塞在某个 NCCL 集合操作上（例如调 `cudaStreamSynchronize`）在某些情况下会造成死锁**——因为某个 rank 上不调 MPI 会阻塞其他 rank，使它们无法到达那个本来能解除第一个 rank 阻塞的 NCCL 调用。

这种情况下，应该把 `cudaStreamSynchronize` 换成如下循环，在等待的同时给 MPI 推进的机会：

```c
cudaError_t err = cudaErrorNotReady;
int flag;
while (err == cudaErrorNotReady) {
  err = cudaStreamQuery(args->streams[i]);
  MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
}
```

### 与 CUDA-aware MPI 并发做 GPU 间通信

**用 NCCL 与 CUDA-aware MPI 并发执行 GPU 间通信可能造成死锁。**

机理：NCCL 会创建**跨设备依赖**——NCCL kernel 一旦启动，就会等待（并可能阻塞该 CUDA 设备）直到 communicator 内所有 rank 都启动了它们的 NCCL kernel。而 CUDA-aware MPI 视实现不同，**也可能在设备之间创建这类依赖**。

结论：**在同一组 CUDA 设备之间并发使用 MPI 和 NCCL 做传输，不保证安全。**

## 差异速查

| 方面 | MPI | NCCL |
| --- | --- | --- |
| rank 与进程映射 | 通常 1:1 | 一个进程可含多 rank/多设备 |
| ReduceScatter 语义 | 向量式（`MPI_Reduce_scatter`） | 规整式，对应 `MPI_Reduce_scatter_block` |
| send/recv count 与类型 | 可不同（字节数相等即可） | 单一 count、单一类型 |
| alltoall / gather / scatter 等 | 有专门接口 | 用 send/recv + group 组合表达 |
| 任意源接收 | `MPI_ANY_SOURCE` | 不支持，必须指定源 rank |
| message tag | 有 | 无 |
