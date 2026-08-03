# 31 · Examples

> 原文：Examples
>
> 原文给出了几个完整可编译的程序（含 CUDACHECK/NCCLCHECK/MPICHECK 宏和 hostHash 辅助函数）。本笔记提取各例的**骨架和关键点**；完整源码请对照原文页面。

原文的示例覆盖以下技术的组合：

- 每线程/每进程多 GPU
- 多线程
- 多进程（示例用 MPI 作并行运行时环境，但任何多进程系统都应能类似工作）

**提醒：始终检查 NCCL 函数的返回码。** 原文示例为清晰起见省略了错误检查（实际完整示例里用宏做了检查）。

## Communicator 创建与销毁

### 例 1：单进程、单线程、多设备

单进程场景可以直接用 `ncclCommInitAll`。为 4 个设备创建 communicator，得到 4 个 communicator 对象：

```c
ncclComm_t comms[4];
int devs[4] = { 0, 1, 2, 3 };
ncclCommInitAll(comms, 4, devs);
```

之后可以用单线程配 group 调用，或者用多线程、每个线程拿一个 comm 对象，来发起集合操作。

程序结束时销毁所有 communicator：

```c
for (int i=0; i<4; i++)
  ncclCommDestroy(comms[i]);
```

完整示例的流程骨架：

```c
int nDev = 4;
int size = 32*1024*1024;
int devs[4] = { 0, 1, 2, 3 };

// 每个设备上分配并初始化 buffer、创建 stream
for (int i = 0; i < nDev; ++i) {
  cudaSetDevice(i);
  cudaMalloc((void**)sendbuff + i, size * sizeof(float));
  cudaMalloc((void**)recvbuff + i, size * sizeof(float));
  cudaMemset(sendbuff[i], 1, size * sizeof(float));
  cudaMemset(recvbuff[i], 0, size * sizeof(float));
  cudaStreamCreate(s + i);
}

ncclCommInitAll(comms, nDev, devs);

// 每线程多设备时必须用 group API
ncclGroupStart();
for (int i = 0; i < nDev; ++i)
  ncclAllReduce((const void*)sendbuff[i], (void*)recvbuff[i], size, ncclFloat, ncclSum,
                comms[i], s[i]);
ncclGroupEnd();

// 在 CUDA stream 上同步以等待 NCCL 操作完成
for (int i = 0; i < nDev; ++i) {
  cudaSetDevice(i);
  cudaStreamSynchronize(s[i]);
}

// 释放 buffer、销毁 communicator
```

### 例 2：每进程或每线程一个设备

当一个进程或 host 线程最多负责一个 GPU 时，`ncclCommInitRank` 可以作为集合调用来创建 communicator，**每个线程或进程得到自己的对象**。

MPI 场景下每 MPI rank 一个设备的创建流程：

```c
// 1. 取 MPI 进程信息
int myRank, nRanks;
MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
MPI_Comm_size(MPI_COMM_WORLD, &nRanks);

// 2. 单个 rank 创建 unique ID 并广播给所有其他 rank
ncclUniqueId id;
if (myRank == 0) ncclGetUniqueId(&id);
MPI_Bcast(&id, sizeof(id), MPI_BYTE, 0, MPI_COMM_WORLD);

// 3. 创建 communicator
ncclComm_t comm;
ncclCommInitRank(&comm, nRanks, id, myRank);

// 4. 使用
ncclAllReduce(..., comm);

// 5. 销毁
ncclCommDestroy(comm);
```

**完整示例中的关键辅助逻辑：如何算 localRank。** 原文用 hostname + `/proc/sys/kernel/random/boot_id` 的 DJB2a 哈希来唯一标识主机（这样裸金属和容器实例都能区分），然后：

```c
uint64_t hostHashs[nRanks];
char hostname[1024];
getHostName(hostname, 1024);
hostHashs[myRank] = getHostHash(hostname);
MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, hostHashs, sizeof(uint64_t), MPI_BYTE, MPI_COMM_WORLD);
for (int p = 0; p < nRanks; p++) {
  if (p == myRank) break;
  if (hostHashs[p] == hostHashs[myRank]) localRank++;
}
```

即：把所有 rank 的主机哈希 allgather 出来，数一下自己前面有多少个 rank 在同一台机器上，这个数就是 localRank，用它来 `cudaSetDevice(localRank)` 选 GPU。

主流程：

```c
if (myRank == 0) ncclGetUniqueId(&id);
MPI_Bcast((void*)&id, sizeof(id), MPI_BYTE, 0, MPI_COMM_WORLD);

cudaSetDevice(localRank);          // 按 localRank 选 GPU
cudaMalloc(&sendbuff, size * sizeof(float));
cudaMalloc(&recvbuff, size * sizeof(float));
cudaStreamCreate(&s);

ncclCommInitRank(&comm, nRanks, id, myRank);

ncclAllReduce((const void*)sendbuff, (void*)recvbuff, size, ncclFloat, ncclSum, comm, s);

cudaStreamSynchronize(s);          // 在 stream 上同步以完成 NCCL 操作

cudaFree(sendbuff); cudaFree(recvbuff);
ncclCommDestroy(comm);
MPI_Finalize();
```

注意这里**不需要 group API**（每进程单设备）。

### 例 3：每线程多设备

多进程/多线程与每进程多设备可以组合，此时**需要 group 语义**。下面把 MPI 与每进程（每 MPI rank）多设备结合：

```c
int myRank, nRanks;
MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
MPI_Comm_size(MPI_COMM_WORLD, &nRanks);

ncclUniqueId id;
if (myRank == 0) ncclGetUniqueId(&id);
MPI_Bcast(id, sizeof(id), MPI_BYTE, 0, 0, MPI_COMM_WORLD);

// 创建 ngpus 个 communicator 对象，它们属于一个 ngpus*nRanks 的更大 group
ncclComm_t comms[ngpus];
ncclGroupStart();
for (int i=0; i<ngpus; i++) {
  cudaSetDevice(devs[i]);
  ncclCommInitRank(comms+i, ngpus*nRanks, id, myRank*ngpus + i);
}
ncclGroupEnd();

// ... 使用 ...

for (int i=0; i<ngpus; i++)
  ncclCommDestroy(comms[i]);
```

**关键点**：总 rank 数是 `ngpus*nRanks`，本进程第 i 个设备的 rank 是 `myRank*ngpus + i`。

完整示例（每进程 2 个 GPU）的对应部分：

```c
int nDev = 2;

// 按 localRank 选 GPU
for (int i = 0; i < nDev; ++i) {
  cudaSetDevice(localRank*nDev + i);
  cudaMalloc(sendbuff + i, size * sizeof(float));
  cudaMalloc(recvbuff + i, size * sizeof(float));
  cudaMemset(sendbuff[i], 1, size * sizeof(float));
  cudaMemset(recvbuff[i], 0, size * sizeof(float));
  cudaStreamCreate(s + i);
}

if (myRank == 0) ncclGetUniqueId(&id);
MPI_Bcast((void*)&id, sizeof(id), MPI_BYTE, 0, MPI_COMM_WORLD);

// init 跨多 GPU 调用，ncclCommInitRank 外必须包 group API
ncclGroupStart();
for (int i=0; i<nDev; i++) {
  cudaSetDevice(localRank*nDev + i);
  ncclCommInitRank(comms+i, nRanks*nDev, id, myRank*nDev + i);
}
ncclGroupEnd();

// 每线程多设备时通信也必须包 group API
ncclGroupStart();
for (int i=0; i<nDev; i++)
  ncclAllReduce((const void*)sendbuff[i], (void*)recvbuff[i], size, ncclFloat, ncclSum,
                comms[i], s[i]);
ncclGroupEnd();

for (int i=0; i<nDev; i++)
  cudaStreamSynchronize(s[i]);
```

注意：**init 的 group 内需要 `cudaSetDevice`，通信的 group 内不需要**（见 `08-group-calls.md`）。

### 例 4：每设备多个 communicator

NCCL 允许每个设备创建多个 communicator。多 MPI 进程、每进程一个设备、每设备多个 communicator：

阻塞式 communicator：

```c
// blocking communicators
cudaSetDevice(localRank);
for (int i = 0; i < commNum; ++i) {
  if (myRank == 0) ncclGetUniqueId(&id);
  MPI_Bcast((void*)&id, sizeof(id), MPI_BYTE, 0, MPI_COMM_WORLD);
  ncclCommInitRank(&blockingComms[i], nRanks, id, myRank);
}
```

非阻塞式 communicator：

```c
// non-blocking communicators
cudaSetDevice(localRank);
ncclConfig_t config = NCCL_CONFIG_INITIALIZER;
config.blocking = 0;
for (int i = 0; i < commNum; ++i) {
  if (myRank == 0) ncclGetUniqueId(&id);
  MPI_Bcast((void*)&id, sizeof(id), MPI_BYTE, 0, MPI_COMM_WORLD);
  ncclCommInitRankConfig(&nonblockingComms[i], nRanks, id, myRank, &config);
  do {
    ncclCommGetAsyncError(nonblockingComms[i], &state);
  } while (state == ncclInProgress && checkTimeout() != true);
}
```

**每个 communicator 都需要一个独立的 unique ID。** `checkTimeout()` 应由用户自己定义。非阻塞 communicator 的更多用法见 `04-creating-a-communicator.md` 的容错一节；若想 split 已有 communicator 而不是新建，见 `ncclCommSplit()`。

## 通信

### 例 1：每进程或每线程一个设备

每个线程为自己的设备调用集合操作：

```c
ncclAllReduce(sendbuff, recvbuff, count, datatype, op, comm, stream);
```

调用后操作已入队到 stream。想等它完成就同步：

```c
cudaStreamSynchronize(stream);
```

### 例 2：每线程多设备

单线程管理多设备时，需要用 group 语义一次性在多个设备上启动操作：

```c
ncclGroupStart();
for (int i=0; i<ngpus; i++)
  ncclAllReduce(sendbuffs[i], recvbuff[i], count, datatype, op, comms[i], streams[i]);
ncclGroupEnd();
```

**`ncclGroupEnd` 之后**所有操作才都已入队到 stream，这时才能同步：

```c
for (int i=0; i<ngpus; i++)
  cudaStreamSynchronize(streams[i]);
```

## 模式选择速查

| 场景 | 初始化方式 | 需要 group API？ | 通信前需 `cudaSetDevice`？ |
| --- | --- | --- | --- |
| 单进程多设备 | `ncclCommInitAll` | 通信需要 | 不需要 |
| 每进程/线程单设备 | `ncclCommInitRank` | 不需要 | 已在 init 前设好 |
| 每进程多设备（配 MPI） | `ncclCommInitRank` 放 group 内 | init 和通信都需要 | init 内需要，通信内不需要 |
| 每设备多 communicator | 每个 comm 一个 unique ID | 视上面情况 | — |
