# 09 · Point-to-point communication

> 原文：Using NCCL → Point-to-point communication

## 双边通信（Two-sided，NCCL 2.7 起）

点对点通信可以表达 rank 之间**任意**通信模式。

一次点对点通信需要**两个** NCCL 调用：一侧 rank 调 `ncclSend()`，另一侧 rank 调对应的 `ncclRecv()`，**count 和 data type 必须相同**。

面向不同 peer 的多个 `ncclSend()` / `ncclRecv()` 可以用 `ncclGroupStart()` / `ncclGroupEnd()` 融合起来，构造更复杂的模式：one-to-all（scatter）、all-to-one（gather）、all-to-all，或 N 维空间中与邻居的通信。

### 关键定序规则

- group 内的点对点调用会**阻塞直到该组调用完成**。
- 但 group 内的各个调用可以看作**独立推进**，因此它们之间不应互相阻塞。
- 所以**必须把需要并发推进的调用合并到同一个 group 里**，否则会死锁。
- **唯一例外**：group 内面向**同一个 peer** 的点对点调用是**按顺序执行**的。

下面是并行应用中几种经典点对点模式。NCCL 语义允许每个 rank 使用不同的 size、datatype 和 buffer。

### Sendrecv

用 MPI 的说法，sendrecv 是两个 rank 交换数据、同时发同时收。把 `ncclSend` 和 `ncclRecv` 合进一个 group：

```c
ncclGroupStart();
ncclSend(sendbuff, sendcount, sendtype, peer, comm, stream);
ncclRecv(recvbuff, recvcount, recvtype, peer, comm, stream);
ncclGroupEnd();
```

### One-to-all（scatter）

从 root rank 出发的 one-to-all，把所有 send 和 recv 合进一个 group：

```c
ncclGroupStart();
if (rank == root) {
  for (int r=0; r<nranks; r++)
    ncclSend(sendbuff[r], size, type, r, comm, stream);
}
ncclRecv(recvbuff, size, type, root, comm, stream);
ncclGroupEnd();
```

### All-to-one（gather）

```c
ncclGroupStart();
if (rank == root) {
  for (int r=0; r<nranks; r++)
    ncclRecv(recvbuff[r], size, type, r, comm, stream);
}
ncclSend(sendbuff, size, type, root, comm, stream);
ncclGroupEnd();
```

### All-to-all

对所有 peer 的 send/recv 循环合并：

```c
ncclGroupStart();
for (int r=0; r<nranks; r++) {
  ncclSend(sendbuff[r], sendcount, sendtype, r, comm, stream);
  ncclRecv(recvbuff[r], recvcount, recvtype, r, comm, stream);
}
ncclGroupEnd();
```

### Neighbor exchange

N 维空间中与邻居交换数据：

```c
ncclGroupStart();
for (int d=0; d<ndims; d++) {
  ncclSend(sendbuff[d], sendcount, sendtype, next[d], comm, stream);
  ncclRecv(recvbuff[d], recvcount, recvtype, prev[d], comm, stream);
}
ncclGroupEnd();
```

## 单边通信（One-sided，NCCL 2.29 起）

单边通信让一个 rank 用 `ncclPutSignal()` **把数据写入远端内存，而不要求目标 rank 发出匹配操作**。

前提条件：

- 目标内存必须**预先用 `ncclCommWindowRegister()` 注册**。
- 点对点同步通过目标 rank 调 `ncclWaitSignal()` 等待 signal 来实现。

### group 内的语义

多个 `ncclPutSignal()` 可以用 `ncclGroupStart()` / `ncclGroupEnd()` 分组：

- group 内面向**不同 peer 或不同 context** 的操作可以并发执行、以任意顺序完成。
- `ncclGroupEnd()` 返回即**保证 group 内所有操作都已达成完成**。
- 面向**同一 peer 和同一 context** 的操作**按顺序执行**：远端 peer 上的数据交付和 signal 更新都遵循程序顺序。

### PutSignal 与 WaitSignal

用 `ncclPutSignal()` / `ncclWaitSignal()` 实现的 ping-pong，含内存分配和 window 注册的完整流程：

```c
// Allocate symmetric memory for RMA operations
void *sendbuff, *recvbuff;
NCCLCHECK(ncclMemAlloc((void**)&sendbuff, size));
NCCLCHECK(ncclMemAlloc((void**)&recvbuff, size));

// Register buffers as symmetric windows
ncclWindow_t sendWindow, recvWindow;
NCCLCHECK(ncclCommWindowRegister(comm, sendbuff, size, &sendWindow, NCCL_WIN_COLL_SYMMETRIC));
NCCLCHECK(ncclCommWindowRegister(comm, recvbuff, size, &recvWindow, NCCL_WIN_COLL_SYMMETRIC));

int peer = (rank == 0) ? 1 : 0;
ncclWaitSignalDesc_t waitDesc = {.opCnt = 1, .peer = peer, .sigIdx = 0, .ctx = ctx};

if (rank == 0) {
  // Rank 0: wait then put
  NCCLCHECK(ncclWaitSignal(1, &waitDesc, comm, stream));
  NCCLCHECK(ncclPutSignal(sendbuff, count, datatype, peer, recvWindow, 0,
                          0, 0, 0, comm, stream));
} else {
  // Rank 1: put then wait
  NCCLCHECK(ncclPutSignal(sendbuff, count, datatype, peer, recvWindow, 0,
                          0, 0, 0, comm, stream));
  NCCLCHECK(ncclWaitSignal(1, &waitDesc, comm, stream));
}

CUDACHECK(cudaStreamSynchronize(stream));

// Cleanup
NCCLCHECK(ncclCommWindowDeregister(comm, sendWindow));
NCCLCHECK(ncclCommWindowDeregister(comm, recvWindow));
NCCLCHECK(ncclMemFree(sendbuff));
NCCLCHECK(ncclMemFree(recvbuff));
```

### Barrier

用 `ncclSignal()` / `ncclWaitSignal()` 实现 barrier：每个 rank 向所有其他 rank 发 signal，并等待来自所有 rank 的 signal：

```c
ncclWaitSignalDesc_t *waitDescs = malloc(nranks * sizeof(ncclWaitSignalDesc_t));
for (int r = 0; r < nranks; r++) {
  waitDescs[r].opCnt = 1;
  waitDescs[r].peer = r;
  waitDescs[r].sigIdx = 0;
  waitDescs[r].ctx = 0;
}

ncclGroupStart();
for (int r = 0; r < nranks; r++) {
  ncclSignal(r, 0, 0, 0, comm, stream);
}
ncclGroupEnd();

ncclWaitSignal(nranks, waitDescs, comm, stream);
```

### All-to-all（单边版）

用 `ncclPutSignal()` 实现，每个 rank 向所有其他 rank 发数据并等待所有 rank 的 signal。

两个前提：

- 需要**事先用 `ncclCommWindowRegister()` 为每个 peer 注册内存 window**。
- 需要**用户自己保证调 `ncclPutSignal()` 前 buffer 已就绪**——可以用上面那个 barrier 来做。

```c
size_t offset[nranks];
ncclWaitSignalDesc_t *waitDescs = malloc(nranks * sizeof(ncclWaitSignalDesc_t));
for (int r = 0; r < nranks; r++) {
  offset[r] = r * count * wordSize(datatype);
  waitDescs[r].opCnt = 1;
  waitDescs[r].peer = r;
  waitDescs[r].sigIdx = 0;
  waitDescs[r].ctx = 0;
}

ncclGroupStart();
for (int r = 0; r < nranks; r++) {
  ncclPutSignal(sendbuff[r], count, datatype, r, window, offset[r],
                0, 0, 0, comm, stream);
}
ncclGroupEnd();

ncclWaitSignal(nranks, waitDescs, comm, stream);
```

## 双边 vs 单边 对比

| | 双边（2.7+） | 单边（2.29+） |
| --- | --- | --- |
| 对端参与 | 必须调匹配的 `ncclRecv` | 不需要 |
| 内存要求 | 普通 CUDA 指针 | 必须 `ncclMemAlloc` + `ncclCommWindowRegister` 注册成 window |
| 同步方式 | send/recv 自身配对 | `ncclSignal` / `ncclWaitSignal` 显式做 |
| group 内定序 | 不同 peer 独立推进；同 peer 按序 | 不同 peer/ctx 可并发任意序完成；同 peer+ctx 按序 |
| buffer 就绪 | 由配对语义保证 | **用户自己保证**（例如先做 barrier） |
