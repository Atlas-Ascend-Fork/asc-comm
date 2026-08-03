# 10 · Thread Safety

> 原文：Using NCCL → Thread Safety

## 基本结论

NCCL 原语**一般不是线程安全的**，但它们是**可重入的（reentrant）**。

多线程环境下明确禁止的行为：

- **不允许**多线程并行地向**同一个 communicator** 发出 NCCL 操作。
- **不安全**：多线程并行地向位于**同一设备上的多个独立 communicator** 发出 NCCL 操作（参见 `04-creating-a-communicator.md` 中"并发使用多个 communicator"）。
- 如果子 communicator 与父 communicator **共享资源**（即 `ncclConfig_t` 中的 `splitShare`），**不允许**并行地向子、父 communicator 发出操作。

## 允许的做法

只要用户能保证**同一时刻只有一个线程在操作该 communicator**，从多个线程操作一个 communicator 是安全的。

但对于**分组的 NCCL 操作**，用户需要确保**由单一线程发出该 group 内的所有操作**。

## 示例一：线程串行执行，同时只有一个线程访问 communicator

```c
Thread 0:
ncclConfig_t config = NCCL_CONFIG_INITIALIZER;
config.blocking = 0;
cudaSetDevice(0);
ncclCommInitRankConfig(&comm, nranks, id, rank, &config);
ncclGroupStart();
ncclAllReduce(sendbuff0, recvbuff0, count0, datatype, redOp, comm, stream);
ncclAllReduce(sendbuff1, recvbuff1, count1, datatype, redOp, comm, stream);
ncclGroupEnd();
thread_exit();

Thread 1:
ncclResult_t state = ncclSuccess;
// wait for previous issued allreduce ops by Thread 0
do {
  ncclCommGetAsyncError(comm, &state);
} while(state == ncclInProgress);
assert(state == ncclSuccess);
ncclAllReduce(sendbuff2, recvbuff2, count2, datatype, redOp, comm, stream);
do {
  ncclCommGetAsyncError(comm, &state);
} while(state == ncclInProgress);
assert(state == ncclSuccess);
```

注意 group 内的两个 allReduce 都由 Thread 0 单独发出，Thread 1 先轮询到 `ncclSuccess` 才接手。

## 示例二：一个线程发出分组操作，各线程分别轮询各自 communicator 状态

```c
Thread 0:
ncclConfig_t config = NCCL_CONFIG_INITIALIZER;
config.blocking = 0;
ncclGroupStart();
for (int i = 0; i < nGpus; i++) {
  cudaSetDevice(i);
  ncclCommInitRankConfig(&comms[i], nranks, id, ranks[i], &config);
}
ncclGroupEnd();

Thread 0/1/2/3:
ncclResult_t state = ncclSuccess;
// wait for previous issued init ops by Thread 0
do {
  ncclCommGetAsyncError(comms[thread_id], &state);
} while(state == ncclInProgress);
assert(state == ncclSuccess);
ncclAllReduce(sendbuff, recvbuff, count, datatype, redOp, comms[thread_id], stream);
do {
  ncclCommGetAsyncError(comms[thread_id], &state);
} while(state == ncclInProgress);
assert(state == ncclSuccess);
```

这种模式：**init 的 group 由单线程统一发出**，之后每个线程各自持有一个 communicator 并独立轮询、独立发操作。

## 速查

| 场景 | 是否允许 |
| --- | --- |
| 多线程并行操作同一个 communicator | ✗ |
| 多线程并行操作同一设备上的多个独立 communicator | ✗（不安全） |
| 并行操作共享资源（`splitShare`）的父子 communicator | ✗ |
| 多线程操作同一 communicator，但严格串行、同时只有一个 | ✓ |
| group 内所有操作由单一线程发出 | ✓（必须） |
| 单线程发出 group init，之后各线程各自持有一个 communicator | ✓ |
