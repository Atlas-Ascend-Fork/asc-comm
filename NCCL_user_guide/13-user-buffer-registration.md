# 13 · User Buffer Registration

> 原文：Using NCCL → User Buffer Registration

## 是什么

User Buffer Registration 让 NCCL **直接通过用户 buffer 收发/处理数据，省掉内部额外拷贝（zero-copy）**。收益是加速集合操作、大幅降低资源占用（例如 channel 使用数）。

NCCL 提供两种注册方式：

- **CUDA Graph 注册**
- **Local 注册**

## 全局硬性约束

对所有 NCCL 通信函数调用（allreduce、sendrecv 等）：

- 如果 communicator 内**任何一个 rank** 传入了已注册的 buffer，**同一 communicator 内所有其他 rank 也必须传入各自已注册的 buffer**。
- 混用已注册和未注册的 buffer 会导致**未定义行为**。
- 此外，**源和目的 buffer 都必须注册**，才能对该 NCCL 操作启用 user buffer registration。

## NVLink SHARP（NVLS）buffer 注册

**2.19.x 起**支持。任何支持 NVLS 算法的 NCCL 集合操作（如 allreduce）都能用上。

### CUDA Graph 方式的要求

1. buffer 通过 `ncclMemAlloc()` 或合格的 allocator 分配（见下文 Memory Allocator）
2. 每个 rank 上，NCCL 操作在**被 CUDA graph capture 的 stream** 上启动
3. 集合操作中，**各 rank 相对 buffer 头地址的 offset 必须相同**

注册的 buffer 在 **CUDA graph 被销毁时自动解注册**。

```c
void* sendbuff;
void* recvbuff;
size_t count = 1 << 25;
CHECK(ncclMemAlloc(&sendbuff, count * sizeof(float)));
CHECK(ncclMemAlloc(&recvbuff, count * sizeof(float)));

cudaGraph_t graph;
CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
CHECK(ncclAllReduce(sendbuff, recvbuff, 1024, ncclFloat, ncclSum, comm, stream));
// Same offset to the sendbuff and recvbuff head address for each rank
CHECK(ncclAllReduce((void*)((float*)sendbuff+1024), (void*)((float*)recvbuff+2048), 1024, ncclFloat, ncclSum, comm, stream));
CHECK(cudaStreamEndCapture(stream, &graph));

cudaGraphExec_t instance;
CHECK(cudaGraphInstantiate(&instance, graph, NULL, NULL, 0));
CHECK(cudaGraphLaunch(instance, stream));
CHECK(cudaStreamSynchronize(stream));
CHECK(cudaGraphExecDestroy(instance));
CHECK(cudaGraphDestroy(graph));

CHECK(ncclMemFree(sendbuff));
CHECK(ncclMemFree(recvbuff));
```

### Local 方式的要求

1. buffer 通过 `ncclMemAlloc()` 或合格 allocator 分配
2. 每个 rank 在调集合操作**之前**用 `ncclCommRegister()` 注册 buffer
3. 照常调集合操作，同样保持各 rank 相对头地址的 offset 相同

注册的 buffer 在用户**显式调 `ncclCommDeregister()`** 时解注册。

```c
void* sendbuff;
void* recvbuff;
size_t count = 1 << 25;
void* sendRegHandle;
void* recvRegHandle;
CHECK(ncclMemAlloc(&sendbuff, count * sizeof(float)));
CHECK(ncclMemAlloc(&recvbuff, count * sizeof(float)));

CHECK(ncclCommRegister(comm, sendbuff, count * sizeof(float), &sendRegHandle));
CHECK(ncclCommRegister(comm, recvbuff, count * sizeof(float), &recvRegHandle));

CHECK(ncclAllReduce(sendbuff, recvbuff, 1024, ncclFloat, ncclSum, comm, stream));
CHECK(ncclAllReduce((void*)((float*)sendbuff+1024), (void*)((float*)recvbuff+2048), 1024, ncclFloat, ncclSum, comm, stream));
CHECK(cudaStreamSynchronize(stream));

CHECK(ncclCommDeregister(comm, sendRegHandle));
CHECK(ncclCommDeregister(comm, recvRegHandle));

CHECK(ncclMemFree(sendbuff));
CHECK(ncclMemFree(recvbuff));
```

Local 方式的用法建议：**程序开始时注册一次，反复复用同一 buffer**，从而摊薄注册成本。

### 用大块 buffer 省内存

也可以分配一大块 buffer 只注册一次，再从中切出 `sendbuff` 和 `recvbuff`，只要满足 offset 要求即可零拷贝：

```c
void* buffer;
void* handle;
void* sendbuff;
void* recvbuff;
size_t size = 1 << 29;

CHECK(ncclMemAlloc(&buffer, size));
CHECK(ncclCommRegister(comm, buffer, size, &handle));

// assign buffer chunk to sendbuff and recvbuff
sendbuff = buffer;
recvbuff = (void*)((uint8_t*)buffer + (1 << 20));

CHECK(ncclAllReduce(sendbuff, recvbuff, 1024, ncclFloat, ncclSum, comm, stream));
CHECK(ncclAllGather(sendbuff, recvbuff, 1024, ncclInt8, comm, stream));
CHECK(cudaStreamSynchronize(stream));

CHECK(ncclCommDeregister(comm, handle));

CHECK(ncclMemFree(sendbuff));
```

## IB SHARP buffer 注册

**2.21.x 起**支持。任何支持 IB SHARP 算法的集合操作都能受益，如 allreduce、reducescatter、allgather。

**当前限制**：只支持**每节点 1 个 rank** 的 communicator。注册可以把 NCCL 的 SM 使用数降到 1。

| 方式 | 步骤 |
| --- | --- |
| CUDA Graph | ① 用任意 CUDA allocator 分配 send/recv buffer（`cudaMalloc`、`ncclMemAlloc` 均可）② 用 CUDA graph 启动 NCCL 集合操作 |
| Local | ① 用任意 CUDA allocator 分配 send/recv buffer ② communicator 内每个 rank 用 `ncclCommRegister` 注册 send/recv buffer ③ 启动 NCCL 集合操作 |

注意这里与 NVLS 不同：**IB SHARP 允许用 `cudaMalloc` 这类传统 allocator**。

## 通用 buffer 注册（General）

**2.23.x 起**支持**节点内** buffer 注册，面向所有节点内点对点通信（如 Allgather Ring），收益是内存压力更小、通信与计算 overlap 表现更好。

启用方式：程序开始时用 `ncclCommRegister` 注册，或者使用 CUDA graph——两者都能为 NCCL 集合操作和 sendrecv 启用节点内 buffer 注册。

### 关于 allocator 的选择

- 用户 buffer 可以通过 **VMM API（`cuMem*`）**、任何基于 VMM 的 allocator，或 `ncclMemAlloc` 分配，这些都可用。
- 通过**传统 CUDA API（如 `cudaMalloc`）**分配的 buffer 也**可以**注册，**但不安全**：执行期可能 hang，失败和 abort 期间可能段错误。因此**不推荐**用传统 buffer 注册。
- **传统 buffer 注册当前默认关闭**，用户可设 `NCCL_LEGACY_CUDA_REGISTER=1` 打开。

## Buffer 注册、GPU Direct RDMA 与 MPS（MLOPart）

为保证 scale-out 通信的最佳性能，推荐使用 `ncclMemAlloc`，或者用带 `gpuDirectRDMACapable` 属性的 `cuMemCreate`。

**不这么做可能迫使 NCCL 使用内部 staging buffer**，从而抵消 user buffer 注册带来的收益。

另外，**混用不同 allocator 分配的 buffer 可能导致未定义行为**。

## Buffer 注册与 PXN

**网络通信（如 InfiniBand）的 buffer 注册与 PXN 天生不兼容。**

- 只要平台支持，**PXN 在 NCCL 中默认启用**，可用于 sendrecv 类操作和集合操作。
- **PXN 启用时，即使用户调了 `ncclCommRegister`，网络 buffer 注册也不会生效。**
- 要启用网络 buffer 注册，需设 `NCCL_PXN_DISABLE=1` 关掉 PXN。

## Memory Allocator

NCCL 提供 `ncclMemAlloc` 帮用户通过 VMM API 分配 buffer，之后可用于 NCCL 注册。

**它是专为 NCCL 设计的，不建议在应用里到处使用 `ncclMemAlloc` 分配的 buffer。**

### 自建 allocator 的要求（进阶）

要给 NVLS UB 写自己的内存 allocator，分配出的 buffer 必须满足：

1. 分配时带 shared flag `CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR`；在支持的 GPU 上还要带 `CU_MEM_HANDLE_TYPE_FABRIC`
2. buffer 物理内存大小是 **CUMEM 推荐 granularity 的整数倍**（即 `cuMemGetAllocationGranularity(…, CU_MEM_ALLOC_GRANULARITY_RECOMMENDED)`）
3. buffer 虚拟头地址**至少按 CUMEM 推荐 granularity 对齐**，且大小是该 granularity 的整数倍

用 VMM API 做通用 buffer 注册时，allocator 需满足与 NVLS UB allocator **相同**的要求。

## Window Registration

**2.27 起**支持 window 注册：把本地 buffer 注册进 NCCL window，从而在 NCCL 中实现**极低延迟、高带宽**的通信。

- 当前 window 注册**只支持来自基于 VMM 的 allocator 和 `ncclMemAlloc` 的输入 buffer**；其他类型的 CUDA buffer 注册会失败。
- **默认启用**。不用的话可设 `NCCL_WIN_ENABLE=0` 关闭。另可通过 Window Registration Flags 中的 flag 控制行为。
- 对 **device API** 而言：对称注册的 window（例如带 `NCCL_WIN_COLL_SYMMETRIC`）提供 **LSA（load/store accessible）内存**——device 代码可以用 load/store 操作访问 peer buffer。device 侧指针访问器和 reduce/copy 操作见 `25-api-device-memory.md`。

```c
void* src;
void* dst;
ncclWindow_t src_win;
ncclWindow_t dst_win;

CHECK(ncclMemAlloc(&src, src_size));
CHECK(ncclMemAlloc(&dst, dst_size));
// Passing NCCL_WIN_COLL_SYMMETRIC requires users to provide the symmetric buffers among all ranks in collectives.
// Every rank needs to call ncclCommWindowRegister to register its buffers.
CHECK(ncclCommWindowRegister(comm, src, src_size, &src_win, NCCL_WIN_COLL_SYMMETRIC));
CHECK(ncclCommWindowRegister(comm, dst, dst_size, &dst_win, NCCL_WIN_COLL_SYMMETRIC));
// Use the registered buffers for communication to enable symmetric communication benefits.
// In this example, every rank has 0x1000 offset and 0x2000 offset from the head address of
// src and dst respectively, which satisfies the symmetric buffer requirement.
CHECK(ncclAllGather((uint8_t*)src + 0x1000, (uint8_t*)dst + 0x2000, 1, ncclInt8, comm, stream));
CHECK(cudaStreamSynchronize(stream));

CHECK(ncclCommWindowDeregister(comm, src_win));
CHECK(ncclCommWindowDeregister(comm, dst_win));

CHECK(ncclMemFree(src));
CHECK(ncclMemFree(dst));
```

更多细节见 `ncclCommWindowRegister()` 与 `ncclCommWindowDeregister()` 的说明。

## Zero-CTA 优化

NCCL 支持 zero-CTA 优化：**通信不占用 CTA**，从而让通信与计算 overlap。

版本支持：

- **NVLink 上通过 Copy Engine（CE）的 zero-CTA**：NCCL **2.28 起**
- **跨网络的 zero-CTA**（节点间走 CPU proxy、节点内走 CE）：NCCL **2.30.6 起**

### 启用条件

1. CUDA driver 版本 **≥ 12.5**
2. buffer **对称注册**进 NCCL window
3. communicator 配置了 **`NCCL_CTA_POLICY_ZERO`** flag（见 NCCL Communicator CTA Policy Flags）

### 支持的集合操作

| 范围 | 支持的操作 |
| --- | --- |
| 单个 NVL 或 MNNVL 域内 | AlltoAll、AllGather、Scatter、Gather |
| 跨网络 | AlltoAll、AllGather |

```c
ncclConfig_t config = NCCL_CONFIG_INITIALIZER;
// NCCL_CTA_POLICY_ZERO to enable zero-CTA optimization whenever possible
config.CTAPolicy = NCCL_CTA_POLICY_ZERO;
CHECK(ncclCommInitRankConfig(&comm, nranks, id, rank, &config));

void* src;
void* dst;
ncclWindow_t src_win;
ncclWindow_t dst_win;

CHECK(ncclMemAlloc(&src, src_size));
CHECK(ncclMemAlloc(&dst, dst_size));

// Register the buffers into NCCL symmetric window
CHECK(ncclCommWindowRegister(comm, src, src_size, &src_win, NCCL_WIN_COLL_SYMMETRIC));
CHECK(ncclCommWindowRegister(comm, dst, dst_size, &dst_win, NCCL_WIN_COLL_SYMMETRIC));

CHECK(ncclAllGather(src, dst, 1, ncclInt8, comm, stream));
CHECK(cudaStreamSynchronize(stream));

CHECK(ncclCommWindowDeregister(comm, src_win));
CHECK(ncclCommWindowDeregister(comm, dst_win));

CHECK(ncclMemFree(src));
CHECK(ncclMemFree(dst));
```

## 版本速查

| 特性 | 起始版本 |
| --- | --- |
| NVLS user buffer 注册 | 2.19.x |
| CUDA Graph 注册 + CollNet | 2.11 |
| IB SHARP buffer 注册 | 2.21.x |
| 通用（节点内）buffer 注册 | 2.23.x |
| Window 注册 | 2.27 |
| Zero-CTA over NVLink (CE) | 2.28 |
| Zero-CTA 跨网络 | 2.30.6 |

## 相关环境变量

`NCCL_LEGACY_CUDA_REGISTER`、`NCCL_PXN_DISABLE`、`NCCL_WIN_ENABLE`、`NCCL_GRAPH_REGISTER`
