# 08 · Group Calls

> 原文：Using NCCL → Group Calls

## 三个用途

`ncclGroupStart` / `ncclGroupEnd` 用于把多个调用合并成一个，需要它的场景有三类：

1. **从一个线程管理多个 GPU**（避免死锁）
2. **聚合通信操作**以提升性能
3. **合并多个 send/receive 点对点操作**（见 `09-point-to-point.md`）

三种用法可以组合使用，**唯一的例外**：`ncclCommInitRank()` 的调用不能和其他调用合并在一起。

## 从一个线程管理多个 GPU

单线程管理多设备时**必须**用 group 语义。原因：每个 NCCL 调用在把操作真正 post 到给定 stream 之前，**可能需要阻塞等待其他线程/rank 到达**。所以下面这种朴素循环可能在第一次调用处就卡住等其余的：

```c
for (int i=0; i<nLocalDevs; i++) {
  ncclAllReduce(..., comm[i], stream[i]);
}
```

> **警告**：不推荐在从单线程管理多 GPU 的场景下使用 CUDA graph capture。某些情况下 `cudaGraphLaunch` 可能阻塞，导致无法在所有 GPU 上完成 launch。详见 `12-cuda-graphs.md`。

要声明这些调用属于同一次集合操作，用 group 包起来：

```c
ncclGroupStart();
for (int i=0; i<nLocalDevs; i++) {
  ncclAllReduce(..., comm[i], stream[i]);
}
ncclGroupEnd();
```

这告诉 NCCL 把 `ncclGroupStart` 和 `ncclGroupEnd` 之间的所有调用当作**一次面向多设备的调用**。

**注意**：group 内部调用的 stream 操作（如 `ncclAllReduce`）**可能在还没把操作入队到 stream 时就返回**。因此 `cudaStreamSynchronize` 这类 stream 操作只能在 `ncclGroupEnd` 返回**之后**调用。

一个线程管理多个设备时，创建 communicator 也必须用 group：

```c
ncclGroupStart();
for (int i=0; i<nLocalDevs; i++) {
  cudaSetDevice(device[i]);
  ncclCommInitRank(comms+i, nranks, commId, rank[i]);
}
ncclGroupEnd();
```

**与 NCCL 1.x 的差异**：group 内每次 NCCL 通信调用前**不需要**再设 CUDA 设备；但 group 内调 `ncclCommInitRank` 时**仍然需要**设。

## 聚合操作（2.2 及以后）

group 语义还可以用来在**单次 NCCL launch 内执行多个集合操作**，好处是降低 launch 开销（即延迟）——多个操作只付一次 launch 代价。

**Init 函数不能与其他 init 函数聚合，也不能与通信函数聚合。**

做法就是在一个 group 区间内多次调用 NCCL。下例把一个 broadcast 和两个 allReduce 作为单次 NCCL launch 发出：

```c
ncclGroupStart();
ncclBroadcast(sendbuff1, recvbuff1, count1, datatype, root, comm, stream);
ncclAllReduce(sendbuff2, recvbuff2, count2, datatype, comm, stream);
ncclAllReduce(sendbuff3, recvbuff3, count3, datatype, comm, stream);
ncclGroupEnd();
```

允许把聚合与多 GPU launch 组合，并在一次 group launch 中使用不同的 communicator。组合时，`ncclGroupStart`/`ncclGroupEnd` 可以只用一层，也可以每层都用。下例把不同 layer、多个 CUDA 设备上的 allReduce 聚合起来：

```c
ncclGroupStart();
for (int i=0; i<nlayers; i++) {
  ncclGroupStart();
  for (int g=0; g<ngpus; g++) {
    ncclAllReduce(sendbuffs[g]+offsets[i], recvbuffs[g]+offsets[i], counts[i], datatype[i], comms[g], streams[g]);
  }
  ncclGroupEnd();
}
ncclGroupEnd();
```

**注意**：NCCL 操作只会在**最后那次** `ncclGroupEnd` 时整体启动。for 循环内的 `ncclGroupStart`/`ncclGroupEnd` 不是必需的，它们什么都不做。

## Group 操作的定序语义

虽然 group 允许一次性发出不同操作，用户仍然必须保证**不同 GPU 之间操作的发出顺序相同**——无论这些操作发到同一个还是不同的 communicator。

下例是**正确**的顺序。`comm0` 和 `comm1` 是包含 rank 0 和 1 的两个相互独立的重复 communicator：

```c
RANK0 / GPU0 / Process0:
ncclGroupStart();
ncclBroadcast(sendbuff1, recvbuff1, count1, datatype, root, comm0, stream);
ncclAllReduce(sendbuff2, recvbuff2, count2, datatype, comm0, stream);
ncclAllReduce(sendbuff3, recvbuff3, count3, datatype, comm0, stream);
ncclAllReduce(sendbuff4, recvbuff4, count4, datatype, comm1, stream);
ncclGroupEnd();

RANK1 / GPU1 / Process1:
ncclGroupStart();
ncclBroadcast(sendbuff1, recvbuff1, count1, datatype, root, comm0, stream);
ncclAllReduce(sendbuff2, recvbuff2, count2, datatype, comm0, stream);
ncclAllReduce(sendbuff3, recvbuff3, count3, datatype, comm0, stream);
ncclAllReduce(sendbuff4, recvbuff4, count4, datatype, comm1, stream);
ncclGroupEnd();
```

改变任何操作的顺序都会导致**结果错误或 hang**。

**错误示例 1 —— 同一 communicator 内两个 allReduce 在两侧顺序相反**：

```c
RANK0 / GPU0 / Process0:
ncclGroupStart();
ncclBroadcast(sendbuff1, recvbuff1, count1, datatype, root, comm0, stream);
ncclAllReduce(sendbuff3, recvbuff3, count3, datatype, comm0, stream); // WRONG: reversed order
ncclAllReduce(sendbuff2, recvbuff2, count2, datatype, comm0, stream); // WRONG: reversed order
ncclAllReduce(sendbuff4, recvbuff4, count4, datatype, comm1, stream);
ncclGroupEnd();

RANK1 / GPU1 / Process1:
ncclGroupStart();
ncclBroadcast(sendbuff1, recvbuff1, count1, datatype, root, comm0, stream);
ncclAllReduce(sendbuff2, recvbuff2, count2, datatype, comm0, stream); // WRONG: reversed order
ncclAllReduce(sendbuff3, recvbuff3, count3, datatype, comm0, stream); // WRONG: reversed order
ncclAllReduce(sendbuff4, recvbuff4, count4, datatype, comm1, stream);
ncclGroupEnd();
```

**错误示例 2 —— 跨 communicator 的相对顺序不一致**（rank0 把 comm1 的操作提到了最前）：

```c
RANK0 / GPU0 / Process0:
ncclGroupStart();
ncclAllReduce(sendbuff4, recvbuff4, count4, datatype, comm1, stream); // WRONG: reversed order
ncclBroadcast(sendbuff1, recvbuff1, count1, datatype, root, comm0, stream);
ncclAllReduce(sendbuff2, recvbuff2, count2, datatype, comm0, stream);
ncclAllReduce(sendbuff3, recvbuff3, count3, datatype, comm0, stream);
ncclGroupEnd();

RANK1 / GPU1 / Process1:
ncclGroupStart();
ncclBroadcast(sendbuff1, recvbuff1, count1, datatype, root, comm0, stream);
ncclAllReduce(sendbuff2, recvbuff2, count2, datatype, comm0, stream);
ncclAllReduce(sendbuff3, recvbuff3, count3, datatype, comm0, stream);
ncclAllReduce(sendbuff4, recvbuff4, count4, datatype, comm1, stream); // WRONG: reversed order
ncclGroupEnd();
```

关键点：**顺序一致性要求是跨 communicator 的**，不只是同一个 communicator 内部。

## 非阻塞 group 操作

如果 communicator 通过 `ncclCommInitRankConfig` 被标记为 nonblocking，group 函数也相应变为异步。此时若在一个 group 内发出多个 NCCL 操作，`ncclGroupEnd()` 返回**不代表** NCCL 通信 kernel 已经被发到 CUDA stream 上：

| `ncclGroupEnd()` 返回值 | 含义 |
| --- | --- |
| `ncclSuccess` | NCCL kernel 已发到 stream |
| `ncclInProgress` | NCCL kernel 正在后台发往 stream |

**用户有责任**在调用相关 CUDA 调用（如 `cudaStreamSynchronize`）之前，确保 communicator 状态变成 `ncclSuccess`：

```c
ncclGroupStart();
for (int g=0; g<ngpus; g++) {
  ncclAllReduce(sendbuffs[g]+offsets[i], recvbuffs[g]+offsets[i], counts[i], datatype[i], comms[g], streams[g]);
}
ret = ncclGroupEnd();
if (ret == ncclInProgress) {
  for (int g=0; g<ngpus; g++) {
    do {
      ncclCommGetAsyncError(comms[g], &state);
    } while (state == ncclInProgress);
  }
} else if (ret == ncclSuccess) {
  /* Successfully issued */
  printf("NCCL kernel issue succeeded\n");
} else {
  /* Errors happen */
  reportErrorAndRestart();
}

for (int g=0; g<ngpus; g++) {
  cudaStreamSynchronize(streams[g]);
}
```
