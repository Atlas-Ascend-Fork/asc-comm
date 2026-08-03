# 14 · Device-Initiated Communication

> 原文：Using NCCL → Device-Initiated Communication

**NCCL 2.28 起**提供 device 侧通信 API，可以**直接在用户 CUDA kernel 内使用通信原语**。

## Device API 的四个模块

| 模块 | 用途 |
| --- | --- |
| **LSA**（Load/Store Accessible） | 通过内存 load/store 操作（CUDA P2P）在设备间通信。包括 NVLink 连接的设备，以及部分有 P2P 连通性的 PCIe 设备（用 `nvidia-smi topo -p2p p` 查看）。**2.28.3 及之前**，LSA 可用性还受 `NCCL_P2P_LEVEL` 距离检查约束，更新版本已取消该约束 |
| **Multimem** | 使用 NVLink SHARP 提供的**硬件 multicast** 特性通信（Hopper 及以后的部分数据中心 GPU 可用） |
| **GIN**（GPU-Initiated Networking） | **跨网络**通信（NCCL 2.28.7 起） |
| **Reduce / Broadcast / Fused 构件** | 计算融合 kernel 的构件：reduce、copy（broadcast）、reduce-then-copy，详见 `27-api-device-reducecopy.md` |

## 要求

Device API 依赖**对称内存**（见 `13-user-buffer-registration.md` 的 Window Registration），后者依赖 GPU 虚拟内存管理（`NCCL_CUMEM_ENABLE`），multimem 支持还可选地依赖 NVLink SHARP（`NCCL_NVLS_ENABLE`）。

### GIN 的要求

- 编译 GPU 代码需 **CUDA 12.2 或更高**
- GPU：**Volta 或更新**；驱动 **≥ 510.40.3**
- NIC：**CX4 或更新**；`rdma-core` **≥ 44.0**
- GPU Direct RDMA：GIN host proxy 需要 DMA-BUF 或 `nvidia-peermem` 支持；GIN GDAKI 需要 DMA-BUF（kernel ≥ 6.1）或 `nvidia-peermem`
- 网络拓扑：需要**完整的 NIC 连通性**。不支持 NIC 无法跨 rail 通信的拓扑，也**不支持 `NCCL_CROSS_NIC=0`**
- **不支持 Fused NIC**。要在双端口 NIC 上用 GIN，设 `NCCL_IB_MERGE_NICS=0`
- 对由**多个 cuMem segment** 支撑的 buffer 使用 GIN 需要 DMA-BUF

使用 **host-backed buffer** 时的额外限制：

- host segment 必须用 `CU_MEM_LOCATION_TYPE_HOST_NUMA` 分配
- 不支持 DirectNIC
- 不支持 LSA Multimem
- 不支持 host RMA API

其他：使用 host RMA API 需 CUDA ≥ 12.5；用 `EMIT_LLVM_IR=1` 构建（生成可读 LLVM IR）需 CUDA 12。

## 跨版本兼容性

NCCL 假定 **device 代码的编译期版本与对应 host 代码（即 `ncclDevCommCreate()` 调用处）的编译期版本相同**。

**2.29 起** host 侧结构体带版本号，以便做跨版本兼容检查。规则：

- 编译期版本**不能新于**运行时版本（即 `libnccl.so` 的版本）。
- 2.29 起，**LSA 和 multimem 的 kernel 支持向后兼容**：用 2.29.2/2.29.3 编译的 kernel 在 2.29.7 下运行应继续可用。
- **GIN 的 kernel 目前不向后兼容**，NCCL 升级后需要重新编译。

## Host 侧 setup

要在 device kernel 中做通信，先要用 `ncclDevCommCreate()` 创建一个 **device communicator**。buffer 上的数据传输操作需要对称内存 window。之后用标准 CUDA 语法启动自定义通信 kernel：

```c
int main() {
  [...]
  NCCLCHECK(ncclCommInitRank(&comm, nranks, id, rank));

  /* Buffer initialization and window creation */
  char* buffer;
  size_t size = 256*1048576;
  NCCLCHECK(ncclMemAlloc((void**)&buffer, size));
  ncclWindow_t win;
  NCCLCHECK(ncclCommWindowRegister(comm, buffer, size, &win, NCCL_WIN_COLL_SYMMETRIC));

  /* Get device communicator */
  ncclDevComm devComm;
  ncclDevCommRequirements reqs = NCCL_DEV_COMM_REQUIREMENTS_INITIALIZER;
  int nCTAs = 16;
  reqs.lsaBarrierCount = nCTAs;
  NCCLCHECK(ncclDevCommCreate(comm, &reqs, &devComm));

  /* Launch user kernel */
  customKernel<<<nCTAs, 512>>>(devComm, win);
  [...]
}
```

按 kernel 和应用需求，输入输出可以共用同一个 window，也可能需要多个 window。创建 device communicator 时，**kernel 需要的资源要通过 requirements 列表指定**（见 `ncclDevCommRequirements`）。上例只指定了 LSA kernel 需要的 barrier 数量——每个 CTA 一个（16 个 CTA，每个 512 线程）。

## 简单 LSA kernel

```c
template<typename T>
__global__ void inPlaceAllReduceKernel(ncclDevComm devComm, ncclWindow_t win, size_t offset, size_t count) {
  ncclLsaBarrierSession<ncclCoopCta> bar{ncclCoopCta(), devComm, ncclTeamTagLsa(), blockIdx.x};
  bar.sync(ncclCoopCta(), cuda::memory_order_acquire);

  const int rank = devComm.lsaRank, nRanks = devComm.lsaSize;
  const int globalTid = threadIdx.x + blockDim.x*(rank + blockIdx.x*nRanks);
  const int globalNthreads = blockDim.x*gridDim.x*nRanks;

  for (size_t o = globalTid; o < count; o += globalNthreads) {
    T v = 0;
    for (int peer = 0; peer < nRanks; peer++) {
      T* inputPtr = (T*)ncclGetLsaPointer(win, offset, peer);
      v += inputPtr[o];
    }
    for (int peer = 0; peer < nRanks; peer++) {
      T* outputPtr = (T*)ncclGetLsaPointer(win, offset, peer);
      outputPtr[o] = v;
    }
  }

  bar.sync(ncclCoopCta(), cuda::memory_order_release);
}
```

这是一个 in-place（输入 buffer 复用为输出）AllReduce，用 LSA 支持——数据通过内存 load/store 指令传输。

要点解释：

- buffer 起点用**已注册 window `win` 内的字节偏移**指定，buffer 含 `count` 个 `T` 类型元素。
- kernel 处理数据前需确保所有参与方就绪：创建内存 barrier session `bar`（见 `ncclLsaBarrierSession`），用它在 CTA 内所有线程（`ncclCoopCta()`，见下文 Thread Groups）和 communicator 的各 rank（`devComm`）之间同步。`ncclTeamTagLsa` 指明该 barrier 作用的 rank 子集（见下文 Teams）——本 kernel 假定所有 rank 都是 LSA 连通的。`blockIdx.x` 是 CTA 的本地索引，用于选择 barrier。
- 然后算出每个线程的全局唯一索引和总线程数，按 **all-to-all 模式**处理数据。外层循环每次迭代中，每个参与线程从每个 communicator rank 各读一个输入元素（第一个内层循环）。
- `ncclGetLsaPointer()` 用来计算各 rank 内 buffer 起点的**本地可访问地址**（远端 device 内存此前已映射进本地地址空间）。累加后把结果写回每个 rank（第二个内层循环）。
- kernel 结束前需要再做一次内存同步，确保所有参与方都处理完了自己的数据。

**注意**：这个简单实现很可能达不到峰值带宽，因为它既没有向量化也没有循环展开。优化过的 LSA reduce / copy / fused reduce-then-copy 构件（用于 AllReduce、AllGather、ReduceScatter）见 `27-api-device-reducecopy.md`。

## Multimem device kernel

Host 侧：

```c
int main() {
  [...]
  reqs = NCCL_DEV_COMM_REQUIREMENTS_INITIALIZER;
  int nCTAs = 16;
  reqs.lsaBarrierCount = nCTAs;
  reqs.lsaMultimem = true;
  NCCLCHECK(ncclDevCommCreate(comm, &reqs, &devComm));
  [...]
}
```

Device 侧：

```c
template<typename T>
__global__ void inPlaceAllReduceKernel(ncclDevComm devComm, ncclWindow_t win, size_t offset, size_t count) {
  ncclLsaBarrierSession<ncclCoopCta> bar{ncclCoopCta(), devComm, ncclTeamTagLsa(), blockIdx.x, /*multimem*/true};
  [...]
  T* mmPtr = (T*)ncclGetLsaMultimemPointer(win, offset, devComm);
  for (size_t o = globalTid; o < count; o += globalNthreads) {
    T v = multimem_sum(mmPtr + o);
    multimem_st(mmPtr + o, v);
  }
  [...]
}
```

改动要点：

- Host 侧：创建 device communicator 前需在 requirements 中设 **`lsaMultimem`**。硬件支持不可用时 `ncclDevCommCreate()` 会失败。
- Device 侧：给 barrier 构造函数**多传一个参数**即可切到 multimem 优化变体。
- **处理循环用 multimem 反而更简单**：`ncclGetLsaMultimemPointer()` 每个 kernel 只需调一次。返回的 multicast 内存指针可以访问 communicator 所有 rank 的 device 内存而**不必逐个遍历**，且**数据在硬件中完成归约**。
- 为保持示例简洁，`multimem_sum` 和 `multimem_st` 的实现未给出，需要用 PTX 实现，例如 `multimem.ld_reduce.global.add` 和 `multimem.st.global`。

## Thread Groups

Device API 中许多函数接受一个**线程协作组**作为输入，用来指明 CTA 内哪些线程参与该操作。NCCL 预定义了三个：

- `ncclCoopThread()`
- `ncclCoopWarp()`
- `ncclCoopCta()` —— 最常用

用户也可以传 CUDA cooperative group，或任何提供 `thread_rank()`、`size()`、`sync()` 方法的类。

## Teams

NCCL 把 communicator 内的 rank 子集称为 **team**，用于寻址远端 rank 或做 barrier。预定义三个：

| Team | 含义 |
| --- | --- |
| `ncclTeamWorld()` | "world" team，包含给定 communicator 的所有 rank |
| `ncclTeamLsa()` | 本 rank 能用 load/store 操作访问到的所有 peer |
| `ncclTeamRail()` | 在各自 LSA team 内具有相同 rank 号的 peer 集合（rail team 与 LSA team 正交） |

`ncclTeam` 结构含 `nRanks`、`rank`、`stride` 三个字段。Device API 提供了检查 team 成员归属、在 team 之间转换 rank 号等函数。

**stride 规律**：world team 和 LSA team 总是连续的（stride 为 `1`）；rail team 通常不连续，其 **stride 等于 LSA team 的大小**——隐含假设是本地 LSA team 内的每个 rank n 与所有远端 LSA team 中对应的 rank n 有直接网络连通性。

## Segment Types

`ncclGin::put()` 和 `ncclGin::get()` 的 `SegmentType` 模板参数描述源和目的虚拟地址的**物理内存构成**，定义了三个 tag 类型：

| Tag | 含义 |
| --- | --- |
| `ncclGin_SegmentDevice`（默认） | 虚拟地址只包含 `CU_MEM_LOCATION_TYPE_DEVICE` 类型的 cuMem segment |
| `ncclGin_SegmentHostNuma` | 只包含 `CU_MEM_LOCATION_TYPE_HOST_NUMA` 类型的 segment |
| `ncclGin_SegmentMixed` | 混合包含 DEVICE 和 HOST_NUMA 两类 segment |

## Host 可访问的 device 指针函数

**2.29 起**，NCCL 提供 host 可访问的函数，让 host 代码能获取指向 LSA 内存区域的指针。四个函数：

| 函数 | 返回 |
| --- | --- |
| `ncclGetLsaMultimemDevicePointer()` | multimem 基址指针 |
| `ncclGetMultimemDevicePointer()` | 带自定义 handle 的 multimem 基址指针 |
| `ncclGetLsaDevicePointer()` | LSA peer 指针 |
| `ncclGetPeerDevicePointer()` | world rank peer 指针 |

这些函数会**从 window 对象自动发现关联的 communicator**，并返回 `ncclResult_t` 错误码。

```c
int main() {
  [...]
  // Allocate symmetric memory buffer
  char* buffer;
  size_t size = 256 * 1024 * 1024;  // 256 MB buffer
  NCCLCHECK(ncclMemAlloc((void**)&buffer, size));

  // Create window with the allocated buffer
  ncclWindow_t win;
  NCCLCHECK(ncclCommWindowRegister(comm, buffer, size, &win, NCCL_WIN_COLL_SYMMETRIC));

  // Get host-accessible pointers
  void* multimemPtr;
  void* lsaPtr;
  void* peerPtr;

  // Get multimem pointer (returns nullptr if multimem not supported)
  NCCLCHECK(ncclGetLsaMultimemDevicePointer(win, 0, &multimemPtr));
  if (multimemPtr == nullptr) {
    // Multimem not available, use fallback
  }

  // Get LSA pointer for peer 1
  NCCLCHECK(ncclGetLsaDevicePointer(win, 0, 1, &lsaPtr));

  // Get peer pointer for world rank 2
  NCCLCHECK(ncclGetPeerDevicePointer(win, 0, 2, &peerPtr));

  // Use pointers in custom kernels or legacy code
  customKernel<<<nCTAs, 256>>>(multimemPtr, lsaPtr, peerPtr);

  // Cleanup
  NCCLCHECK(ncclCommWindowDeregister(comm, &win));
  // Device pointers are invalidated after window deregistration
  NCCLCHECK(ncclMemFree(buffer));
  [...]
}
```

**重要**：指针生命周期受限于 **window 和 communicator 生命周期中较短的那个**。这些函数应该**只调一次并缓存指针复用**。

## GIN device kernel

下面是纯 GIN 的 AlltoAll：所有 peer 数据都走网络。Host 侧创建带 GIN 专用资源的 `ncclDevComm`，注册对称内存 window，然后启动用 GIN 做集合操作的 kernel。

```c
// Grid width (CTAs). Must match reqs.worldGinBarrierCount and reqs.ginSignalCount.
#define NCCL_DEVICE_CTA_COUNT 16
#define NCCL_DEVICE_THREADS_PER_CTA 512

int main() {
  [...]
  ncclDevCommRequirements reqs = NCCL_DEV_COMM_REQUIREMENTS_INITIALIZER;
  reqs.worldGinBarrierCount = NCCL_DEVICE_CTA_COUNT;
  reqs.ginSignalCount = NCCL_DEVICE_CTA_COUNT;
  reqs.ginConnectionType = NCCL_GIN_CONNECTION_FULL;
  NCCLCHECK(ncclDevCommCreate(comm, &reqs, &devComm));
  [...]
}

template<typename T>
__global__ void PureGinAlltoAllKernel(ncclWindow_t sendwin, size_t sendoffset,
                                      ncclWindow_t recvwin, size_t recvoffset,
                                      size_t count, struct ncclDevComm devComm) {
  int ginContext = 0;  // single context for simplicity
  unsigned int signalIndex = blockIdx.x;
  ncclGin gin{devComm, ginContext};
  uint64_t signalValue = gin.readSignal(signalIndex);

  ncclGinBarrierSession<ncclCoopCta> bar{ncclCoopCta(), gin, ncclTeamTagWorld(), blockIdx.x};
  bar.sync(ncclCoopCta(), cuda::memory_order_acquire, ncclGinFenceLevel::None);

  int tid = threadIdx.x + blockIdx.x * blockDim.x;
  int nthreads = blockDim.x * gridDim.x;

  const size_t size = count * sizeof(T);
  for (int r = tid; r < devComm.nRanks; r += nthreads) {
    gin.put(ncclTeamWorld(devComm), r,
            recvwin, recvoffset + devComm.rank * size,
            sendwin, sendoffset + r * size,
            size, ncclGin_WeakSignalInc{signalIndex});
  }

  // Wait only on the CTA whose blockIdx.x (signalIndex) accumulates all puts to this rank.
  int receivingCta = (devComm.rank % nthreads) / blockDim.x;
  if (blockIdx.x == receivingCta)
    gin.waitSignal(ncclCoopCta(), signalIndex, signalValue + devComm.nRanks);

  gin.flush(ncclCoopCta());
  bar.sync(ncclCoopCta(), cuda::memory_order_release, ncclGinFenceLevel::None);
}
```

以上是 **NCCL 2.30 及以后**的写法。GIN 自 2.28.7 起可用；更老版本的差异见下文"兼容性调整"。

### Host 侧字段说明

- `worldGinBarrierCount` 为 `ncclGinBarrierSession`（网络侧 barrier）预留槽位。
- `ginSignalCount` 为完成通知预留 per-CTA signal。
- 两者都设为 launch grid 的 CTA 数（这里是 `NCCL_DEVICE_CTA_COUNT`），与 `gridDim.x` 匹配，于是每个 thread block 用 `blockIdx.x` 同时作为自己的 barrier 索引和 signal 索引。GIN 依赖这些 barrier 和 signal 做跨 rank 同步、跟踪异步工作。
- `ginConnectionType` 设为 `NCCL_GIN_CONNECTION_FULL` 表示把每个 rank 连到所有 peer（见 `ncclGinConnectionType_t`）。
- 若无法提供 GIN，`ncclDevCommCreate()` 会失败。

### Device 侧逻辑说明

- GIN barrier **跨 rank 通过网络同步**。每个 thread block 用 `blockIdx.x` 选自己的 barrier，从而能与其他节点上对应的 block 协调。
- 这里用单个 GIN context，构造 `ncclGin` 时传 context 索引 `0`。每个 thread block 在 kernel 入口 `bar.sync()` 之前先读自己的 per-CTA signal 槽（`signalIndex == blockIdx.x`）。
- `ncclGinBarrierSession` 使用 `ncclTeamTagWorld()` 和 `blockIdx.x`。入口处的 barrier 确保 AlltoAll 交换前所有 rank 就绪。
- 与 AllReduce 类 kernel 不同，**AlltoAll 的 per-thread 索引只需在本 rank 内唯一**，该索引用来选目标 peer。
- 主数据传输用单边的 `put()`，在所有参与线程上并行发起，每个目标 peer 一次 `put()`。当 communicator 大小超过参与循环的线程数时需要这个循环。
- `put()` 的参数：目标 rank、目的与源 window 及偏移、传输大小、可选 action。本例传 `ncclGin_WeakSignalInc{signalIndex}` 作为 remoteAction，使目标 rank 在负载落定后收到一次完成计数递增。

### signal 与等待逻辑

- 每个 CTA 在自己发出的 `ncclGin::put()` 上使用 `signalIndex = blockIdx.x`。在目标 rank 上，每个 peer 的 `put()` 对**由该发送方 CTA 的 `blockIdx.x` 索引的 signal 槽**贡献一次递增。
- 所有 CTA 都参与发 `put()`，但**只有 receiving CTA**（本 rank 上单个 thread block）需要观察该 rank signal 槽的完成情况。kernel 用 `receivingCta = (devComm.rank % nthreads) / blockDim.x` 确定，正好那个 block 对 `signalIndex == receivingCta` 执行 `waitSignal()`；其余 CTA 跳过 `waitSignal()`，只发 `put()` 和之后的 `flush()`。
- receivingCta 监视的 signal 被递增 `nRanks` 次后，说明每个 peer 都已把自己的数据放进本 rank 的接收 buffer，buffer 可以消费了。`waitSignal()` 用 `signalValue + devComm.nRanks` 作为阈值阻塞等待——因为每个 peer 发一次入向 `put()`，各推进本 rank 该 `signalIndex` 的计数一次。
- 结束前所有 CTA 仍要调 `flush()` 提交未完成的出向 `put()`。**`flush()` 不保证每个副作用都远端完全完成**，但它确保**从本 kernel 视角看本地 send buffer 可以安全复用**。
- `waitSignal()` 和 `flush()` 之后再跑一次 `bar.sync()`，确保所有 rank 都完成该集合操作后才有 rank 退出 kernel。

### 兼容性调整

上面的 host setup、kernel 和解释对应 **NCCL 2.30 及以后**。针对更老的构建版本：

| 版本条件 | 需要的调整 |
| --- | --- |
| GIN 基线 | GIN 自 **2.28.7** 起可用。`ncclDevCommCreate()` 和 `ncclGin` 要求 communicator 支持 device API 和 GIN |
| **2.30 之前**：无 `worldGinBarrierCount` | `ncclGinBarrierSession` 当时只能用于 rail 连通性，对应字段是 `railGinBarrierCount`。要做 world-team GIN barrier，把 `barrierCount` 设为 CTA 数（同 `gridDim.x`）；kernel 里改用混合的 `ncclBarrierSession` 配 `ncclTeamTagWorld()` 加 `ncclGin`，而不是 `ncclGinBarrierSession` 配 `ncclTeamTagWorld()` |
| **2.29.7 之前**：无 `ginConnectionType` | 把 `ginForceEnable` 设为 `true` 来启用完整 GIN 连通性（等价于有 `ginConnectionType` 后的 `NCCL_GIN_CONNECTION_FULL`）。`ginConnectionType` 字段自 2.29.7 起提供 |
| `ginForceEnable` 已废弃 | 2.29.7 及以后优先用 `ginConnectionType`；`ginForceEnable` 自 2.29.7 起废弃 |
