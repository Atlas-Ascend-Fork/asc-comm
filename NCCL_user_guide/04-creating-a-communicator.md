# 04 · Creating a Communicator

> 原文：Using NCCL → Creating a Communicator

## 基本规则

创建 communicator 时，必须给参与其中的 n 个 CUDA 设备各分配一个 0 到 n-1 之间的唯一 rank。

**同一个 CUDA 设备不支持在同一个 NCCL communicator 里充当多个不同 rank，这样做可能导致 hang。**

给定 rank 到 CUDA 设备的静态映射，以下三个函数会创建 communicator 对象，每个对象绑定一个固定的 rank 和 CUDA 设备，之后用这些对象来发起通信操作：

- `ncclCommInitRank()`
- `ncclCommInitRankConfig()`
- `ncclCommInitAll()`

## ncclGetUniqueId：让各方认出彼此

调用 `ncclCommInitRank()` 之前，先要创建一个唯一对象，供所有进程和线程用来同步、确认自己属于同一个 communicator。这个对象由 `ncclGetUniqueId()` 产生。

`ncclGetUniqueId()` 返回的 ID 必须**通过任意 CPU 侧通信手段广播给所有参与的线程和进程**，例如把 ID 指针传给多个线程，或者用 MPI / socket 等并行环境广播到其他进程。

## ncclCommInitAll：单进程便捷版

`ncclCommInitAll` 可以在**单个进程内**一次创建 n 个 communicator 对象。因为局限于单进程，**这个函数不支持跨节点通信**。它等价于 `ncclGetUniqueId` + `ncclCommInitRank` 的组合，简化实现如下：

```c
ncclResult_t ncclCommInitAll(ncclComm_t* comm, int ndev, const int* devlist) {
  ncclUniqueId Id;
  ncclGetUniqueId(&Id);
  ncclGroupStart();
  for (int i=0; i<ndev; i++) {
    cudaSetDevice(devlist[i]);
    ncclCommInitRank(comm+i, ndev, Id, i);
  }
  ncclGroupEnd();
}
```

## 带 options 创建（ncclCommInitRankConfig）

`ncclCommInitRankConfig()` 允许用特定 options 创建 communicator，可配参数见 `ncclConfig_t`。

例如把 `blocking` 设为 0，要求 NCCL 在任何调用中都不阻塞，同时还可以设置其他参数来更精确地定义 communicator 行为：

```c
ncclConfig_t config = NCCL_CONFIG_INITIALIZER;
config.blocking = 0;
config.minCTAs = 4;
config.maxCTAs = 16;
config.cgaClusterSize = 2;
config.netName = "Socket";
CHECK(ncclCommInitRankConfig(&comm, nranks, id, rank, &config));
do {
  CHECK(ncclCommGetAsyncError(comm, &state));
  // Handle outside events, timeouts, progress, ...
} while(state == ncclInProgress);
```

## 用多个 ncclUniqueId 创建（ncclCommInitRankScalable）

`ncclCommInitRankScalable()` 支持用多个 `ncclUniqueId` 创建 communicator。

- **所有 rank 必须提供同一个 ncclUniqueId 数组**（相同的 id，且顺序相同）。
- 内部实现上，每个 rank 大多只与单个 `ncclUniqueId` 通信，所以为获得最佳性能，**应把这些 id 尽可能均匀地分散到各 rank 上**。

判断某个 rank 是否应该创建 `ncclUniqueId` 的辅助函数：

```c
bool rankHasRoot(const int rank, const int nRanks, const int nIds) {
  const int rmr = nRanks % nIds;
  const int rpr = nRanks / nIds;
  const int rlim = rmr * (rpr + 1);
  if (rank < rlim) {
    return !(rank % (rpr + 1));
  } else {
    return !((rank - rlim) % rpr);
  }
}
```

例：3 个 id 分配到 7 个 rank，第一个 id 对应 rank 0-2，另两个分别对应 rank 3-4 和 5-6。因此该函数在 rank 0、3、5 上返回 true，其余返回 false。

**注意**：只有第一个 `ncclUniqueId` 会用于生成 communicator 的 hash id，该 hash id 用于在日志文件和 replay 工具中标识 communicator。

## 收缩 communicator（ncclCommShrink）

`ncclCommShrink()` 通过从已有 communicator 中**移除特定 rank** 来创建新 communicator。适用场景：需要把某些 GPU 或节点排除在集合操作之外，比如容错场景，或动态调整资源占用。

排除 rank 1 的例子：

```c
int excludeRanks[] = {1};   // Rank to exclude
int excludeCount = 1;       // Number of ranks to exclude
ncclComm_t newcomm;

// Only ranks that will be in the new communicator should call ncclCommShrink
if (myRank != 1) {
  ncclResult_t res = ncclCommShrink(comm, excludeRanks, excludeCount, &newcomm, NULL, NCCL_SHRINK_DEFAULT);
  if (res != ncclSuccess) {
    // Handle error
  }
  // Use the new communicator for collective operations
  // ...
  // When done, destroy the new communicator
  ncclCommDestroy(newcomm);
}
```

从通信错误中恢复时，用 error 模式：

```c
if (myRank != 1) {
  // When shrinking after an error, use NCCL_SHRINK_ABORT to abort operations on the parent communicator
  // This mode is also useful when there might be ongoing operations on the parent communicator
  ncclResult_t res = ncclCommShrink(comm, excludeRanks, excludeCount, &newcomm, NULL, NCCL_SHRINK_ABORT);
  // ...
}
```

要点：

- **只有将要成为新 communicator 成员的 rank 才调用** `ncclCommShrink()`。
- 排除列表里的 rank **不应调用**该函数。
- 新 communicator 会**重新编号 rank** 以保持连续。
- 可以用 `ncclGroupStart` / `ncclGroupEnd` 机制来同步新 communicator 的创建。

## 扩展 communicator（ncclCommGrow）

`ncclCommGrow()` 通过向已有 communicator **添加新 rank** 来创建新 communicator，用于运行中动态扩容。

流程需要已有 rank（来自父 communicator）和新加入 rank 之间的协调：由父 communicator 中的一个 **coordinator rank** 调用 `ncclCommGetUniqueId()` 生成唯一标识，再通过 **out-of-band 机制**（MPI、socket、共享内存等）分发给所有新 rank。

把 4-rank communicator 扩到 8 rank：

```c
// Step 1: Coordinator (e.g., rank 0) generates the grow identifier
ncclUniqueId growId;
if (myRank == 0) {
  ncclResult_t res = ncclCommGetUniqueId(comm, &growId);
  if (res != ncclSuccess) {
    // Handle error
  }
  // Distribute growId to all new ranks using out-of-band communication
  // (e.g., MPI_Send, sockets, shared memory, etc.)
}

// Step 2: All existing ranks call ncclCommGrow
ncclComm_t newcomm;
ncclResult_t res = ncclCommGrow(comm, 8, NULL, -1, &newcomm, NULL);
if (res != ncclSuccess) {
  // Handle error
}

// Step 3: New ranks (4-7) call ncclCommGrow with the received growId
cudaSetDevice(myDevice);
ncclComm_t newcomm;
ncclResult_t res = ncclCommGrow(NULL, 8, &growId, myNewRank, &newcomm, NULL);

// Step 4: Wait for grow operation to complete (if non-blocking)
ncclResult_t asyncErr;
do {
  res = ncclCommGetAsyncError(newcomm, &asyncErr);
} while (asyncErr == ncclInProgress);

// Step 5: Use the new communicator for collective operations
// ...

// Step 6: Existing ranks should destroy the parent communicator
ncclCommDestroy(comm);

// Step 7: When done, destroy the new communicator
ncclCommDestroy(newcomm);
```

非阻塞 grow 带错误处理：

```c
ncclConfig_t config = NCCL_CONFIG_INITIALIZER;
config.blocking = 0;  // Non-blocking mode

// Existing ranks
ncclComm_t newcomm;
ncclResult_t res = ncclCommGrow(comm, 8, NULL, -1, &newcomm, &config);

// Poll for completion
ncclResult_t asyncErr;
do {
  res = ncclCommGetAsyncError(newcomm, &asyncErr);
  if (res != ncclSuccess) {
    // Handle error
    ncclCommAbort(newcomm);
    break;
  }
  // Handle timeouts or other events
} while (asyncErr == ncclInProgress);

if (asyncErr == ncclSuccess) {
  // Grow completed successfully
  // Destroy parent communicator
  ncclCommDestroy(comm);
}
```

重要注意事项：

| 项 | 说明 |
| --- | --- |
| Coordinator 选择 | 父 communicator 中任意 rank 都可以当 coordinator，由它调 `ncclCommGetUniqueId()` 生成 grow 标识 |
| Rank 分配 | 已有 rank 在新 communicator 中**保留原 rank 号**；新 rank 必须从父 communicator 的 size 开始编号 |
| Out-of-band 通信 | grow 标识必须用 NCCL 之外的机制（MPI、socket、共享文件）从 coordinator 分发给所有新 rank |
| 父 communicator 清理 | grow 成功后，已有 rank 应调 `ncclCommDestroy()` 销毁父 communicator 释放资源 |
| 不能有未完成操作 | 调 `ncclCommGrow()` 时父 communicator 上**不应有未完成的 NCCL 操作**，否则可能死锁 |
| 配置继承 | 已有 rank 在新 communicator 上继承父配置；新 rank 用传入的配置或默认值 |

## 创建更多 communicator（ncclCommSplit）

`ncclCommSplit` 基于已有 communicator 创建新的，可以：拆分成多个子分区、复制一个已有 communicator、或创建一个 rank 更少的 communicator。

- **原 communicator 中所有 rank 都必须调用** `ncclCommSplit`。不属于任何子组的 rank 也要调用，只是 color 传 `NCCL_SPLIT_NOCOLOR`。
- 新建的 communicator **继承父 communicator 的配置**（例如 non-blocking）。
- 如果父 communicator 处于非阻塞模式，可以先对父 communicator 调 `ncclCommAbort`、再对返回的任何新 communicator 调，以中止一个 `ncclCommSplit` 操作。之所以两个都要，是因为 hang 可能发生在其中任一个 communicator 的操作上。

复制一个已有 communicator：

```c
int rank;
ncclCommUserRank(comm, &rank);
ncclCommSplit(comm, 0, rank, &newcomm, NULL);
```

对半拆分：

```c
int rank, nranks;
ncclCommUserRank(comm, &rank);
ncclCommCount(comm, &nranks);
ncclCommSplit(comm, rank/(nranks/2), rank%(nranks/2), &newcomm, NULL);
```

只保留前 2 个 rank：

```c
int rank;
ncclCommUserRank(comm, &rank);
ncclCommSplit(comm, rank<2 ? 0 : NCCL_SPLIT_NOCOLOR, rank, &newcomm, NULL);
```

## 并发使用多个 communicator

**2.26 之前**：每设备使用多个 communicator 时，必须把所有通信操作的顺序序列化成一个一致的全局总序（通过 CUDA stream 依赖或同步），否则可能死锁。

**2.26 起**：引入 `NCCL_LAUNCH_ORDER_IMPLICIT`，启用后会**按操作从 host 发出的顺序动态地隐式建立这个全局序**。因此要保持无死锁，用户必须确保**所有设备上 host 侧 launch 的顺序一致**。最简单的做法是每设备用单个 host 线程按确定顺序发出：

```c
ncclAllReduce(..., comm1, stream1); // all ranks do this first
ncclAllReduce(..., comm2, stream2); // and this second
```

**与 CUDA Graph 配合时，capture 时和 launch 时都遵循同样规则。**

capture 时，同一个 graph 内的 NCCL 调用必须以相同顺序被 capture：

```c
// both stream1 and stream2 are capturing in the same graph
ncclAllReduce(..., comm1, stream1); // all ranks do this first
ncclAllReduce(..., comm2, stream2); // and this second
```

graph launch 时，不同 graph 必须以全局一致的顺序启动：

```c
cudaGraphLaunch(graph1, stream1); // all ranks do this first
cudaGraphLaunch(graph2, stream2); // and this second
```

在 **CUDA 12.3 及以后**运行时，这种隐式定序是用 CUDA launch completion event 实现的，因此**允许两个 communicator 的 kernel 并行执行**。

## Finalize communicator

`ncclCommFinalize` 会：

- 把 communicator 从 `ncclSuccess` 状态转到 `ncclInProgress` 状态
- 在后台开始完成所有操作
- 与其他可能正在使用相关资源的 rank 做同步
- 刷新并释放该 communicator 所有未完成操作和网络相关资源

所有 NCCL 操作完成后，communicator 转回 `ncclSuccess` 状态，可用 `ncclCommGetAsyncError` 查询。若 communicator 标记为 nonblocking，此操作非阻塞；否则阻塞。

## Destroy communicator

communicator finalize 之后，下一步是释放所有资源，包括 communicator 本身，用 `ncclCommDestroy`。

- 调用时若 communicator 状态是 `ncclSuccess`，该调用**保证非阻塞**；否则可能阻塞。
- 任何情况下，`ncclCommDestroy` 都会释放资源并返回，**返回后不得再访问该 communicator**。

## 错误处理与 communicator abort

所有 NCCL 调用都返回错误码。若返回的错误码不是 `ncclSuccess` 也不是 `ncclInternalError`，且 `NCCL_DEBUG=WARN`，NCCL 会打印一条人可读的消息解释发生了什么；若 `NCCL_DEBUG=INFO`，还会打印导致错误的调用栈。

| 错误 | 说明 | 解决方向 | 错误处理 | Group 行为 |
| --- | --- | --- | --- | --- |
| `ncclSuccess` | 无错误 | — | — | — |
| `ncclUnhandledCudaError` | CUDA 调用出错 (1) | CUDA 配置/用法 (1) | Communicator abort (5) | Global (6) |
| `ncclSystemError` | 系统调用出错 (1) | 系统配置/用法 (1) | Communicator abort (5) | Global (6) |
| `ncclInternalError` | NCCL 内部错误 (2) | 需在 NCCL 中修复 (2) | Communicator abort (5) | Global (6) |
| `ncclInvalidArgument` | 参数非法 (3) | 修应用 (3) | 无 (3) | Individual (3) |
| `ncclInvalidUsage` | 用法非法 (4) | 修应用 (4) | Communicator abort (5) | Global (6) |
| `ncclInProgress` | 调用仍在进行中 | 用 `ncclCommGetAsyncError` 轮询完成 | 无 | 无 |

注解：

1. `ncclUnhandledCudaError` 和 `ncclSystemError` 表示 NCCL 对外部组件的调用失败，从而导致 NCCL 操作失败。错误消息会说明该看哪个组件，可能需要系统管理员协助。
2. `ncclInternalError` 表示 **NCCL 的 bug**。`NCCL_DEBUG=WARN` 下可能不报消息，因为它需要改 NCCL 源码才能修；`NCCL_DEBUG=INFO` 会打印导致错误的 back trace。
3. `ncclInvalidArgument` 表示参数值不对，比如 NULL 指针或越界值。返回该错误时**这次 NCCL 调用没有产生任何效果**：group 状态不变，communicator 仍正常工作，应用可以调 `ncclCommAbort`，也可以当这次调用没发生过继续跑。group 内的调用会**立即**返回该错误，且只针对那次具体调用；`ncclGroupEnd` 不会返回它（因为 `ncclGroupEnd` 不接受参数）。
4. `ncclInvalidUsage` 在动态条件导致失败时返回，表示 NCCL API 用法不正确。
5. 这些错误对 communicator 是**致命的**。要恢复，应用需对该 communicator 调 `ncclCommAbort` 并重新创建。
6. group 内操作的动态错误**总是由 `ncclGroupEnd` 报告**，且适用于 group 内所有操作（这些操作可能完成也可能没完成）。应用必须对 group 内**所有** communicator 调 `ncclCommAbort`。

### 异步错误与错误处理

部分通信错误、尤其是网络错误，是通过 `ncclCommGetAsyncError` 报告的。**遇到异步错误的操作通常不会推进、也永远不会完成。** 发生异步错误时，应该中止该操作并用 `ncclCommAbort` 销毁 communicator。

等待 NCCL 操作完成时，应用应该调 `ncclCommGetAsyncError`，出错就销毁 communicator。下面是替代 `cudaStreamSynchronize` 的写法：

```c
int ncclStreamSynchronize(cudaStream_t stream, ncclComm_t comm) {
  cudaError_t cudaErr;
  ncclResult_t ncclErr, ncclAsyncErr;
  while (1) {
   cudaErr = cudaStreamQuery(stream);
   if (cudaErr == cudaSuccess)
     return 0;

   if (cudaErr != cudaErrorNotReady) {
     printf("CUDA Error : cudaStreamQuery returned %d\n", cudaErr);
     return 1;
   }

   ncclErr = ncclCommGetAsyncError(comm, &ncclAsyncErr);
   if (ncclErr != ncclSuccess) {
     printf("NCCL Error : ncclCommGetAsyncError returned %d\n", ncclErr);
     return 1;
   }

   if (ncclAsyncErr != ncclSuccess) {
     // An asynchronous error happened. Stop the operation and destroy
     // the communicator
     ncclErr = ncclCommAbort(comm);
     if (ncclErr != ncclSuccess)
       printf("NCCL Error : ncclCommDestroy returned %d\n", ncclErr);
     // Caller may abort or try to create a new communicator.
     return 2;
   }

   // We might want to let other threads (including NCCL threads) use the CPU.
   sched_yield();
  }
}
```

## 容错（Fault Tolerance）

NCCL 提供一组特性让应用能从致命错误（网络故障、节点故障、进程故障）中恢复。发生此类错误时，应用应该能对 communicator 调 `ncclCommAbort` 释放所有资源，然后创建新 communicator 继续。

更进阶的恢复方式：用 `ncclCommShrink` 配 `NCCL_SHRINK_ABORT`，从已有 communicator 中移除故障 rank 来创建新 communicator，同时安全处理进行中的操作。这在只有部分 rank 故障的分布式环境中特别有用。

安全 abort 的前提条件：

- **必须把 communicator 设为 nonblocking**，并确保调 `ncclCommAbort` 时没有线程正在调用任何 NCCL 操作。
- 设了 nonblocking 之后，除 `ncclCommDestroy`/`Abort` 外所有 NCCL 调用都变成非阻塞的，于是 `ncclCommAbort` 可以在任意时刻调用——初始化中、通信中、finalize 中都行。
- 如果 communicator 是阻塞的，线程可能因网络错误卡在 NCCL 调用里，**communicator 可能永久 hang**。

正确 abort 的要求：当 communicator 中任何一个 rank 失败（例如 segfault），**其他所有 rank 都需要调 `ncclCommAbort`** 中止自己的 communicator。何时、是否 abort 并重启 NCCL 操作，由用户自己实现判断逻辑。

非阻塞方式初始化并 split，允许在任意点 abort 的例子：

```c
bool globalFlag;
bool abortFlag = false;
ncclConfig_t config = NCCL_CONFIG_INITIALIZER;
/* set communicator as nonblocking */
config.blocking = 0;
CHECK(ncclCommInitRankConfig(&comm, nRanks, id, myRank, &config));
do {
  CHECK(ncclCommGetAsyncError(comm, &state));
} while (state == ncclInProgress && checkTimeout() != true);

if (checkTimeout() == true || state != ncclSuccess) abortFlag = true;

/* sync abortFlag among all healthy ranks. */
reportErrorGlobally(abortFlag, &globalFlag);

if (globalFlag) {
  /* time is out or initialization failed: every rank needs to abort and restart. */
  ncclCommAbort(comm);
  /* restart NCCL; this is a user implemented function, it might include
   * resource cleanup and ncclCommInitRankConfig() to create new communicators. */
  restartNCCL(&comm);
}

/* nonblocking communicator split. */
CHECK(ncclCommSplit(comm, color, key, &childComm, &config));
do {
  CHECK(ncclCommGetAsyncError(comm, &state));
} while (state == ncclInProgress && checkTimeout() != true);

if (checkTimeout() == true || state != ncclSuccess) abortFlag = true;

/* sync abortFlag among all healthy ranks. */
reportErrorGlobally(abortFlag, &globalFlag);

if (globalFlag) {
  ncclCommAbort(comm);
  /* if chilComm is not NCCL_COMM_NULL, user should abort child communicator
   * here as well for resource reclamation. */
  if (childComm != NCCL_COMM_NULL) ncclCommAbort(childComm);
  restartNCCL(&comm);
}
/* application workload */
```

`checkTimeout` 需要用户自己提供，用来决定应用最多等多久 NCCL 初始化；除超时之外也可以用其他方法检测错误。同样的方法也适用于 NCCL finalization。

## Quality of Service（QoS）

有通信 overlap 的应用可能受益于网络 QoS。NCCL 允许应用给每个 communicator 指定一个 **traffic class（TC）**，用来标识该 communicator 的通信需求，该 communicator 上所有网络操作都会使用这个 TC。

- TC 的具体含义**取决于 communicator 所用的网络插件**：IB 网络用 service level，RoCE 网络用 type of service。
- TC 由系统配置定义。应用必须先了解系统上有哪些 TC 可用、它们的相对行为，才能有效使用。

TC 在创建 communicator 时通过 `ncclConfig_t` 指定：

```c
ncclConfig_t config = NCCL_CONFIG_INITIALIZER;
config.trafficClass = 1;
CHECK(ncclCommInitRankConfig(&comm, nranks, id, rank, &config));
```

### IB 网络

IB 通过 Service Level（SL）支持 QoS。每个 IB SL 映射到一个 Virtual Lane（VL），VL 定义流量的相对优先级。SL 行为在 subnet manager（如 OpenSM）中定义。示例配置：

```
...
qos_max_vls 2
qos_high_limit 255
qos_vlarb_high 1:4
qos_vlarb_low 0:1,1:4
qos_sl2vl 0,1

max_op_vls 2
....
```

这个例子定义了一个低优先级和一个高优先级 VL，分别映射到 SL 0 和 SL 1。高优先级 SL 在每个端口上分到更大的带宽份额。在 NCCL 中，communicator 的 traffic class 对应 IB 网络上的 SL。用这份配置，应用可以给低优先级 communicator 分配 TC 0，给高优先级的分配 TC 1。

### RoCE 网络

communicator 的 `trafficClass` 被解释为 **IP Type of Service（ToS）**。如何为特定负载配置 QoS 请参考网络管理工具。
