# 18 · API · Group Calls

> 原文：NCCL API → Group Calls
>
> 用法层面的说明见 `08-group-calls.md`，本章只讲三个函数的接口语义。

Group 原语定义的是**当前线程**的行为，目的是避免阻塞。因此它们可以被多个线程独立使用。

## ncclGroupStart

```c
ncclResult_t ncclGroupStart()
```

开始一次 group 调用。

在此之后直到 `ncclGroupEnd` 之前的所有 NCCL 调用，**都不会因为 CPU 间同步而阻塞**。

## ncclGroupEnd

```c
ncclResult_t ncclGroupEnd()
```

结束一次 group 调用。

- 当 `ncclGroupStart` 以来的所有操作都已被处理时返回。
- **"已处理"的含义**：通信原语已被入队到给定的 stream，**但不一定已完成**。
- 与 `ncclCommInitRank` 一起使用时，`ncclGroupEnd` 会**等待所有 communicator 初始化完毕**。

## ncclGroupSimulateEnd

```c
ncclResult_t ncclGroupSimulateEnd(ncclSimInfo_t* simInfo)
```

模拟一次 `ncclGroupEnd()` 调用，把 NCCL 的模拟信息返回到作为参数传入的结构体中。

用途：在不真正执行通信的情况下，获取 NCCL 对这组操作的预估信息（`ncclSimInfo_t` 见 `20-api-types.md`）。
