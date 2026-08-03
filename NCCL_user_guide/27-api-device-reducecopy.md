# 27 · API · Device API – Remote Reduce 与 Copy 构件

> 原文：NCCL API → Device API → Device API – Remote Reduce and Copy: Building Blocks for Custom Communication Kernels

本页所有函数**只能从 device（GPU）代码调用**。它们是**计算融合 kernel 的构件**：实现 reduce、copy（broadcast）以及融合的 reduce-then-copy 操作，把通信和计算保持在单个 kernel 内。

## 关键前提

**通信模式**：源和目的可以位于远端 rank（以 `ncclWindow_t` 作为 API 的输入输出），从而可直接实现 AllReduce、AllGather、ReduceScatter 等模式。

**构件而非算法**：每个函数实现一个**峰值带宽通信构件**，不是完整算法。可以把这些构件（以及你自己的计算）组合起来实现自定义通信模式。三个构件：

| 构件 | 语义 | 典型用途 |
| --- | --- | --- |
| `ReduceSum` | reduce | AllReduce 或 ReduceScatter 的 reduce 阶段 |
| `Copy` | broadcast / copy | AllReduce 的 broadcast 阶段，或 AllGather 中的 copy |
| `ReduceSumCopy` | 融合的 reduce-then-copy | 一步式 AllReduce，或 ReduceScatter 的 reduce-to-chunks |

非 sum 的归约见文末"自定义归约算子"。

**API 形态**：全部 device-only，两种形态——**高层便捷重载**（下面各节描述的直接求和重载，与 NCCL window、team、device communicator 配合）和**基于 lambda 的重载**（自定义 layout 更灵活）。

**GIN**：本 API **不隐式支持 GIN**。请在 LSA 域内使用本 API，并用 NCCL GIN 实现显式的分层设计来在 LSA 域之间交换数据。

**调用模型——不是 rank-collective**：与 host API（如 `ncclAllReduce()`）不同，这些函数**不是 rank-collective 的**。对于给定内存区域（即 window + offset + count 的组合），**只能有单个 rank 发出使用该区域的 API 调用**。各操作章节会说明每个角色是 multi-rank（每 rank 为自己的区域发出）还是 single-rank（一个 rank 为该区域发出）。

**内存**：源和目的区域**不得重叠**，除了恰好 in-place 的情况（同一 buffer 同一 offset）；否则行为未定义。**调用方必须保证所有参数和 buffer layout 满足文档要求——API 不做运行时检查。**

**对齐**：为获得最佳性能，使用 **16 字节对齐**的源和目的指针。

## 编译期要求

### NCCL_DEVICE_PERMIT_EXPERIMENTAL_CODE

**在 include NCCL device 头文件之前必须定义为 1**，这样 multimem 操作才支持所有 block size 和类型组合。

不定义它的后果：multimem 操作中某些低精度类型、`count` 和指针对齐的组合可能触发**运行时 assert**。**由于可能只有一个 rank 触发 assert，这还会导致 hang。**

定义它意味着用户认可自己愿意使用可能在版本间变动的前沿 API。

### CUDA extended lambdas

本 API 对接受可调用对象（如 lambda）的重载使用 device 侧 C++ lambda。**为本 API include NCCL device 头文件的代码必须始终在启用 CUDA extended lambda 的情况下编译**（例如 nvcc 加 `--extended-lambda`），否则可能遇到编译期 static assert。

## 公共模板参数

### T —— 元素类型

支持：`float`、`double`、`half`、`int8`、`int16`、`int32`、`int64`；以及在可用时的低精度类型 `__nv_bfloat16`、`__nv_fp8_e4m3`、`__nv_fp8_e5m2`。

**低精度类型的 sum 归约在更宽的类型中累加**：

| T | 累加类型 |
| --- | --- |
| `half` | `float` |
| `__nv_bfloat16` | `float` |
| `__nv_fp8_e4m3` | `half` |
| `__nv_fp8_e5m2` | `half` |

对于 multimem reduce，这个更宽的累加**在 NVLink Switch 上执行**。

### Coop —— 协作级别

见 `14` 的 Thread Groups，例如 `ncclCoopCta` 或 `ncclCoopThread`。**`Coop` 定义的协作组内所有线程都必须参与该调用。**

### IntCount —— 元素计数类型

用户可按 API 操作的 block 区域大小选择 32 位整数类型（如 `unsigned int`）或 64 位整数类型（如 `size_t`）。

### UNROLL —— 可选，默认 `4*16/sizeof(T)`

`UNROLL` 代表**寄存器用量与可达峰值带宽之间的权衡**，最优值取决于周围 kernel 的寄存器用量。

- 更高的 `UNROLL` 允许向量化 load/store 和更多循环展开，有助于达到峰值带宽。
- **但高寄存器用量会降低 occupancy，可能导致寄存器溢出。**
- 默认值的选择是让大多数系统上都能有不错的性能。

示例（`T = float`、`Coop = ncclCoopCta`、`IntCount = size_t`、`UNROLL` 取 float 的默认值 `4*16/sizeof(float) = 16`）：

```c
size_t srcOffset = [...]; // byte offset into symmetric send buffer on each peer
size_t dstOffset = [...]; // byte offset into symmetric recv buffer on each peer
ncclLsaReduceSumCopy<float, ncclCoopCta, size_t, 16>(ctaCoop, sendwin, srcOffset, recvwin, dstOffset, count, team);
```

## ReduceSum —— N 个源到 1 个目的

所有 ReduceSum 变体都用 sum 从 N 个源归约到一个目的。

### 基于 window 的 LSA 版本

```c
template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaReduceSum(Coop coop, ncclWindow_t window, size_t offset, T* dstPtr,
                      IntCount count, ncclDevComm_t devComm)
```

从**所有 LSA peer** 上 `window + offset` 处的对称 buffer 归约到本地 `dstPtr`。归约覆盖 communicator 内所有 LSA rank。

参数要求：

| 参数 | 要求 |
| --- | --- |
| `window` | 来自此前 host 侧 `ncclCommWindowRegister` 的 window handle，**必须与 `devComm` 属于同一 window（和 communicator）**；buffer 区域在调用期间必须保持注册状态 |
| `offset` | 各 peer 上源 buffer 起点在 window 内的字节偏移。**`offset + count × sizeof(T)` 不得超过已注册 window 的大小** |
| `dstPtr` | 指向目的 buffer 的本地 device 指针，**必须至少能容纳 `count` 个 `T` 类型元素**，且必须能被 `coop` 指定的所有参与线程访问 |
| `count` | 要归约的元素数，**必须在所有 LSA rank 上相同**、非负、且与 `IntCount` 相容 |

**Barrier 用法：使用远端内存时，调用前后都要同步。**

```c
ncclCoopCta ctaCoop;
ncclLsaBarrierSession<ncclCoopCta> bar{ctaCoop, devComm, ncclTeamLsa(devComm), devComm.lsaBarrier, blockIdx.x};
bar.sync(ctaCoop, cuda::memory_order_acquire);

size_t srcOffset = [...]; // byte offset into symmetric send buffer on each peer
size_t dstOffset = [...]; // byte offset into symmetric recv buffer on each peer
T* dstPtr = (T*)ncclGetLocalPointer(recvwin, dstOffset);

ncclLsaReduceSum<T, ncclCoopCta, size_t>(ctaCoop, sendwin, srcOffset, dstPtr, count, devComm);

bar.sync(ctaCoop, cuda::memory_order_release);
```

### 显式传 team 的版本

```c
template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaReduceSum(Coop coop, ncclWindow_t window, size_t offset, T* dstPtr,
                      IntCount count, ncclTeam team)
```

同上，但用户显式传 `team`（例如 `ncclTeamLsa(devComm)`）而非 `devComm`。其余参数、调用方式、barrier 用法均同上。

```c
ncclTeam team = ncclTeamLsa(devComm);
ncclLsaReduceSum<T, ncclCoopCta, size_t>(ctaCoop, sendwin, srcOffset, dstPtr, count, team);
```

### 基于 ncclSymPtr 的版本

```c
template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaReduceSum(Coop coop, ncclSymPtr<T> src, T* dstPtr, IntCount count, ncclDevComm_t devComm)

template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaReduceSum(Coop coop, ncclSymPtr<T> src, T* dstPtr, IntCount count, ncclTeam team)
```

源用对称指针 `src` 给出，而非 (window, offset)。**用 `ncclSymPtr` 可以以 0 偏移构造，然后用 `src + elementOffset`——偏移以元素为单位，不需要乘 `sizeof(T)`。**

```c
ncclSymPtr<float> src{sendwin, 0};
ncclLsaReduceSum<float, ncclCoopCta, size_t>(ctaCoop, src + elementOffset, dstPtr, count, devComm);
```

### 纯本地版本

```c
template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLocalReduceSum(Coop coop, int nSrc, T* basePtr, size_t displ, T* dstPtr, IntCount count)
```

与 `ncclLsaReduceSum` 相同，但**只覆盖本地源**（不涉及其他 rank、不涉及远端内存）。

源是 **strided** 的：第 i 个源位于 `basePtr + i*displ`，i 从 0 到 `nSrc − 1`。`displ` 是**字节**步长。

### Multimem reduce

Multimem reduce 使用 NVLink SHARP（NVLS）multicast，**由 NVLink Switch 从 multimem 源执行归约**。

要从 host 查询 NVLS/multimem 能力，调 `ncclCommQueryProperties()` 并检查 `ncclCommProperties_t` 的 `multimemSupport` 字段。

**Multimem 限制**：**本地 rank（self）必须始终参与 multimem 归约或存储**——multimem 源或目的在逻辑上包含调用方 rank。

multimem reduce 支持的元素类型：`float`、`double`、`half`；可用时的低精度类型 `__nv_bfloat16`、`__nv_fp8_e4m3`、`__nv_fp8_e5m2`；以及 `int32`、`uint32`、`int64`、`uint64`。**某些类型与 block size 组合可能需要设 `NCCL_DEVICE_PERMIT_EXPERIMENTAL_CODE=1`。**

三个重载，区别只在源怎么给：

```c
// ncclSymPtr + handle
void ncclMultimemReduceSum(Coop coop, ncclSymPtr<T> src, T* dstPtr, IntCount count,
                           ncclMultimemHandle multimemHandle)

// 裸 multimem 指针（例如来自 host 侧 ncclGetLsaMultimemDevicePointer()）
void ncclMultimemReduceSum(Coop coop, T* mcSrcPtr, T* dstPtr, IntCount count)

// window + 字节 offset + handle
void ncclMultimemReduceSum(Coop coop, ncclWindow_t window, size_t offset, T* dstPtr,
                           IntCount count, ncclMultimemHandle multimemHandle)
```

**如何获得 `multimemHandle`**：调 `ncclDevCommCreate()` 时在 `ncclDevCommRequirements` 中把 `lsaMultimem` 设为 true，之后在 device 代码中从 device communicator 取得。

## Copy（Broadcast）—— 1 个源到 N 个目的

所有 Copy 变体从一个源复制到 N 个目的。**对 Copy 而言，源在发起调用的那个 rank 本地。**

```c
template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaCopy(Coop coop, T* srcPtr, ncclWindow_t window, size_t offset,
                 IntCount count, ncclDevComm_t devComm)

template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaCopy(Coop coop, T* srcPtr, ncclWindow_t window, size_t offset,
                 IntCount count, ncclTeam team)

template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaCopy(Coop coop, T* srcPtr, ncclSymPtr<T> dst, IntCount count, ncclDevComm_t devComm)

template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaCopy(Coop coop, T* srcPtr, ncclSymPtr<T> dst, IntCount count, ncclTeam team)
```

从本地 `srcPtr` 复制到**所有 LSA peer** 上 `window + offset` 处的对称 buffer。使用远端内存时同样需要调用前后同步。

用 `ncclSymPtr` 的版本可以以 0 偏移构造 `dst`，然后用 `dst + elementOffset` 做按元素索引。

示例（AllGather 的 broadcast 阶段）：

```c
ncclCoopCta ctaCoop;
ncclLsaBarrierSession<ncclCoopCta> bar{ctaCoop, devComm, ncclTeamLsa(devComm), devComm.lsaBarrier, blockIdx.x};
bar.sync(ctaCoop, cuda::memory_order_acquire);

size_t srcOffset = [...]; // byte offset into symmetric send buffer on each peer
size_t dstOffset = [...]; // byte offset into symmetric recv buffer on each peer
T* srcPtr = (T*)ncclGetLocalPointer(sendwin, srcOffset);
ncclLsaCopy<T, ncclCoopCta, size_t>(ctaCoop, srcPtr, recvwin, dstOffset, count, devComm);

bar.sync(ctaCoop, cuda::memory_order_release);
```

### 纯本地版本

```c
template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLocalCopy(Coop coop, T* srcPtr, int nDst, T* basePtr, size_t displ, IntCount count)
```

线程协作行为同 `ncclLsaCopy`，但**只涉及本地**：从单个源 `srcPtr` 复制到 `nDst` 个 strided 目的（`basePtr + i*displ`，i 从 0 到 `nDst − 1`）。`displ` 是字节步长，`count` 是复制到**每个**目的的元素数。

### Multimem copy

从一个本地源复制到一个 multimem 目的（一个逻辑 buffer 覆盖所有 rank），使用 NVLS multicast。

```c
void ncclMultimemCopy(Coop coop, T* srcPtr, ncclSymPtr<T> dst, IntCount count,
                      ncclMultimemHandle multimemHandle)

void ncclMultimemCopy(Coop coop, T* srcPtr, T* mcDstPtr, IntCount count)

void ncclMultimemCopy(Coop coop, T* srcPtr, ncclWindow_t window, size_t offset,
                      IntCount count, ncclMultimemHandle multimemHandle)
```

**所有元素类型都支持**；但对于宽度小于 32 位的类型，某些 `count` 和指针组合**必须**设 `NCCL_DEVICE_PERMIT_EXPERIMENTAL_CODE=1`。

## ReduceSumCopy

把归约和复制合并成单次调用。

### LSA ReduceSumCopy

```c
template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaReduceSumCopy(Coop coop, ncclWindow_t srcWindow, size_t srcOffset,
                          ncclWindow_t dstWindow, size_t dstOffset,
                          IntCount count, ncclDevComm_t devComm)

template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaReduceSumCopy(Coop coop, ncclWindow_t srcWindow, size_t srcOffset,
                          ncclWindow_t dstWindow, size_t dstOffset,
                          IntCount count, ncclTeam team)

template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaReduceSumCopy(Coop coop, ncclSymPtr<T> src, ncclSymPtr<T> dst,
                          IntCount count, ncclDevComm_t devComm)

template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaReduceSumCopy(Coop coop, ncclSymPtr<T> src, ncclSymPtr<T> dst,
                          IntCount count, ncclTeam team)
```

一次调用完成：从 `srcWindow + srcOffset`（所有 LSA peer）归约，并把结果复制到 `dstWindow + dstOffset`（所有 LSA peer）。offset 都是字节偏移。使用远端内存时 barrier 用法同前。

示例（LSA AllReduce；block 并行分块可参考原文提到的 `test/perf/all_reduce.cu`）：

```c
ncclCoopCta ctaCoop;
ncclLsaBarrierSession<ncclCoopCta> bar{ctaCoop, devComm, ncclTeamLsa(devComm), devComm.lsaBarrier, blockIdx.x};
bar.sync(ctaCoop, cuda::memory_order_acquire);

size_t srcOffset = [...]; // byte offset into symmetric send buffer on each peer
size_t dstOffset = [...]; // byte offset into symmetric recv buffer on each peer
ncclLsaReduceSumCopy<T, ncclCoopCta, size_t>(ctaCoop, sendwin, srcOffset, recvwin, dstOffset, count, devComm);

bar.sync(ctaCoop, cuda::memory_order_release);
```

### 源和目的用不同 team

```c
template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLsaReduceSumCopy(Coop coop, ncclSymPtr<T> src, ncclTeam srcTeam,
                          ncclSymPtr<T> dst, ncclTeam dstTeam, IntCount count)
```

源和目的使用**不同的 team**（`srcTeam` 和 `dstTeam`）。

**约束**：一个 team 中的 rank 仍必须能从另一个 team 的 rank 做 load-store 访问（同一 LSA communicator；相关 rank 必须能访问彼此已注册的内存）。

### 纯本地版本

```c
template<typename T, typename Coop, typename IntCount, int UNROLL>
void ncclLocalReduceSumCopy(Coop coop, int nSrc, T* srcBasePtr, size_t srcDispl,
                            int nDst, T* dstBasePtr, size_t dstDispl, IntCount count)
```

只涉及本地。从 `nSrc` 个 strided 源（`srcBasePtr + i*srcDispl`）归约，把结果复制到 `nDst` 个 strided 目的（`dstBasePtr + j*dstDispl`）。两个 `Displ` 都是字节步长，`count` 是每个源/目的的元素数。

### Multimem ReduceSumCopy

从一个 multimem 源归约并复制到一个 multimem 目的（各自一个逻辑 buffer 映射到所有参与 rank）。

```c
void ncclMultimemReduceSumCopy(Coop coop, ncclWindow_t srcWindow, size_t srcOffset,
                               ncclMultimemHandle srcHandle,
                               ncclWindow_t dstWindow, size_t dstOffset,
                               ncclMultimemHandle dstHandle, IntCount count)

void ncclMultimemReduceSumCopy(Coop coop, ncclSymPtr<T> src, ncclMultimemHandle srcHandle,
                               ncclSymPtr<T> dst, ncclMultimemHandle dstHandle, IntCount count)

void ncclMultimemReduceSumCopy(Coop coop, T* mcSrcPtr, T* mcDstPtr, IntCount count)
```

`srcHandle` 和 `dstHandle` 标识 multimem context，**可以相同也可以不同**。

### 混合 LSA 与 Multimem

一次调用内从 LSA 归约后写到 multimem，或从 multimem 归约后写到 LSA。

```c
// LSA 源 → multimem 目的
void ncclLsaReduceSumMultimemCopy(Coop coop, ncclSymPtr<T> src, ncclTeam srcTeam,
                                  ncclSymPtr<T> dst, ncclMultimemHandle dstHandle, IntCount count)

void ncclLsaReduceSumMultimemCopy(Coop coop, ncclSymPtr<T> src, ncclTeam srcTeam,
                                  T* mcDstPtr, IntCount count)

// multimem 源 → LSA 目的
void ncclMultimemReduceSumLsaCopy(Coop coop, ncclSymPtr<T> src, ncclMultimemHandle srcHandle,
                                  ncclSymPtr<T> dst, ncclTeam dstTeam, IntCount count)

void ncclMultimemReduceSumLsaCopy(Coop coop, T* mcSrcPtr, ncclSymPtr<T> dst,
                                  ncclTeam dstTeam, IntCount count)
```

## 基于 Lambda 的重载（自定义 layout）

Lambda 重载提供更大灵活性，允许 reduce 和/或 copy 使用自定义内存 layout。它们可以**混合本地与 LSA 远端的源或目的**（例如一个源来自本地内存、其他来自 LSA window），也能表达固定 window/symptr 重载不支持的**非连续或依赖索引的寻址**。

### 对 lambda 的条件

可调用对象必须返回 `T*`，且可从 device 代码以**单个索引参数**调用。若是 lambda，**必须用 `__device__` 限定**，且构建时必须启用 CUDA extended lambda。设 `n` 为相关计数（`nSrc` 或 `nDst`，取决于 API）：

- **`n > 0`**，否则行为未定义。
- 对 `[0, n)` 内每个索引 i，`lambda(i)` **必须返回指向至少 `count` 个连续 `T` 元素的有效区域起点的指针**。约束与对应的非 lambda API 相同：LSA 源/目的的区域必须在已注册的 LSA 内存中且在调用期间保持有效；本地指针必须能被 `coop` 内所有线程访问。
- 当 API 把某一侧指定为 **multimem** 时，该侧 lambda 返回的每个指针**都必须是 multimem 指针**。在 multimem 侧使用 LSA 或本地指针是未定义行为；反过来，multimem 指针也不能用在接受 LSA 或本地指针的地方。
- `n` 与源/目的逻辑集合的关系按各 API 的文档理解（如 `nSrc` 个源意味着 `nSrc` 个不同的源区域）。

**违反任何条件都是未定义行为——API 不做运行时检查。**

示例：从一个 window 上按 team 归约，但**排除本地 rank**（只从远端 peer 归约）。用一个把索引 i 映射到第 i 个其他 rank 的源 lambda，并传 `nSrc = team.nRanks − 1`：

```c
size_t srcOffset = [...]; // byte offset into symmetric send buffer on each peer
ncclTeam team = ncclTeamLsa(devComm);
int myRank = devComm.rank;
int nSrc = team.nRanks - 1; // all ranks except this one

auto srcLambda = [=] __device__ (int i) -> T* {
  int peer = (i < myRank) ? i : i + 1; // skip myRank
  return (T*)ncclGetLsaPointer(sendwin, srcOffset, peer);
};

ncclLsaReduceSum<T, ncclCoopCta, size_t>(ctaCoop, srcLambda, nSrc, dstPtr, count);
```

### ReduceSum / Copy 的 lambda 形式

```c
template<typename T, typename Coop, typename SrcLambda, typename IntCount, int UNROLL>
void ncclLsaReduceSum(Coop coop, SrcLambda srcLambda, int nSrc, T* dstPtr, IntCount count)

template<typename T, typename Coop, typename SrcLambda, typename IntCount, int UNROLL>
void ncclLocalReduceSum(Coop coop, SrcLambda srcLambda, int nSrc, T* dstPtr, IntCount count)

template<typename T, typename Coop, typename DstLambda, typename IntCount, int UNROLL>
void ncclLsaCopy(Coop coop, T* srcPtr, DstLambda dstLambda, int nDst, IntCount count)

template<typename T, typename Coop, typename DstLambda, typename IntCount, int UNROLL>
void ncclLocalCopy(Coop coop, T* srcPtr, DstLambda dstLambda, int nDst, IntCount count)
```

`srcLambda` 以索引 0 到 `nSrc − 1` 调用；`dstLambda` 以索引 0 到 `nDst − 1` 调用。带 `Local` 前缀的版本只涉及本地。

### ReduceSumCopy 的四个 lambda 形式

四者的区别在于**源和目的分别是 LSA 还是 multimem**。对任何 multimem 侧，常见情形是**单个 multimem 指针**（一个指针已经代表多个远端空间）。也支持多个 multimem 指针，此时 API 会对它们全部发起 multicast。

> **警告**：**multicast 总是包含 self rank。** 当有多于一个 multimem 源或目的时，这会造成 rank 重叠，**正确性由用户自己保证**。

```c
// 源 LSA，目的 LSA
void ncclLsaReduceSumLsaCopy(Coop coop, SrcLambda srcLambda, int nSrc,
                             DstLambda dstLambda, int nDst, IntCount count)

// 源 LSA，目的 multimem（常见 nDst = 1）
void ncclLsaReduceSumMultimemCopy(Coop coop, SrcLambda srcLambda, int nSrc,
                                  DstLambda dstLambda, int nDst, IntCount count)

// 源 multimem，目的 LSA（常见 nSrc = 1）
void ncclMultimemReduceSumLsaCopy(Coop coop, SrcLambda srcLambda, int nSrc,
                                  DstLambda dstLambda, int nDst, IntCount count)

// 源 multimem，目的 multimem（常见单源单目的）
void ncclMultimemReduceSumMultimemCopy(Coop coop, SrcLambda srcLambda, int nSrc,
                                       DstLambda dstLambda, int nDst, IntCount count)
```

每种情况下，multimem 那一侧的 lambda **必须**返回 multimem 指针（例如来自 `ncclGetLsaMultimemDevicePointer()`），LSA 那一侧返回 LSA 指针。

## 自定义归约算子

以下 API 接受一个**显式归约算子 `redOp`** 而非固定的 sum，从而支持自定义归约（min、max、product 等）。

### 对 redOp 的限制

- `redOp` 是可调用对象（functor 或 lambda），接受两个 `T`（或 `const T&`）类型参数，返回 `T`（合并后的值）。
- **不保证元素合并的顺序**——归约可能以任意顺序跨源应用。
- 该可调用对象**必须是 const 的**：不得修改内部状态。若 `redOp` 是 functor，其 `operator()` 必须是 `const`；无状态 lambda 默认满足。**违反此约束是未定义行为。**

### 函数

```c
// 源 LSA，目的 LSA
template<typename T, typename Coop, typename SrcLambda, typename DstLambda,
         typename RedOp, typename IntCount, int UNROLL>
void ncclLsaReduceLsaCopy(Coop coop, SrcLambda srcLambda, int nSrc,
                          DstLambda dstLambda, int nDst,
                          RedOp const& redOp, IntCount count)

// 源 LSA，目的 multimem
template<typename T, typename Coop, typename SrcLambda, typename DstLambda,
         typename RedOp, typename IntCount, int UNROLL>
void ncclLsaReduceMultimemCopy(Coop coop, SrcLambda srcLambda, int nSrc,
                               DstLambda dstLambda, int nDst,
                               RedOp const& redOp, IntCount count)
```

**关键约束**：`ncclLsaReduceMultimemCopy` 的 `dstLambda` 必须返回 multimem 指针。**要用自定义归约算子，就用「LSA 源 + multimem 目的」这个组合——因为 multimem 硬件路径只支持 sum。**
