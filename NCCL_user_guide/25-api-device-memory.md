# 25 · API · Device API – Memory 与 LSA

> 原文：NCCL API → Device API → Device API – Memory and LSA

本章讲 device 侧的内存与 LSA（load/store accessible）功能。host 可访问的 device 指针函数见 `24-api-device-setup.md`。

## LSA

以下均为 **device 函数，只能从 device（GPU）代码调用**。下面的指针访问器都基于 LSA。

### ncclLsaBarrierSession

```c
template<typename Coop>
class ncclLsaBarrierSession
```

表示一次内存 barrier session 的类。

#### 构造函数

```c
ncclLsaBarrierSession(Coop coop, ncclDevComm const& comm, ncclTeamTagLsa tag,
                      uint32_t index, bool multimem = false)
```

| 参数 | 含义 |
| --- | --- |
| `coop` | 协作组（见 `14` 的 Thread Groups） |
| `comm` | 用 `ncclDevCommCreate()` 创建的 device communicator |
| `tag`（`ncclTeamTagLsa`） | 指明该 barrier 作用于哪个 rank 子集（见 `14` 的 Teams） |
| `index` | 要使用的底层 barrier 的标识符。**每个 `coop` 应该不同**，通常设为 `blockIdx.x` 以保证 CTA 之间唯一 |
| `multimem` | 请求使用内存 multicast 的硬件加速实现 |

#### 成员函数

```c
void arrive(Coop, cuda::memory_order order)
```
通知该线程已到达 barrier session。

```c
void wait(Coop, cuda::memory_order order)
```
阻塞，直到所有 team 成员的所有线程都到达 barrier session。

```c
void sync(Coop, cuda::memory_order order)
```
同步参与该 barrier session 的所有 team 成员的所有线程（相当于 `arrive` + `wait`）。

### ncclGetPeerPointer

```c
void* ncclGetPeerPointer(ncclWindow_t w, size_t offset, int peer)
```

返回指向 window `w` 内设备 `peer` 的内存 buffer 的 **load/store 可访问指针**。

- `offset` 是**字节**偏移。
- `peer` 是 **world team 内**的 rank 索引（见 `14` 的 Teams）。
- **若该 peer 不在 LSA team 内，返回 NULL。**

### ncclGetLsaPointer

```c
void* ncclGetLsaPointer(ncclWindow_t w, size_t offset, int lsaPeer)
```

与 `ncclGetPeerPointer` 类似，但 `lsaPeer` 是 **LSA team 内**的 rank 索引。

`offset` 同为字节偏移。基于 LSA 内存的高层 reduce 和 copy 操作见 `27-api-device-reducecopy.md`。

### ncclGetLocalPointer

```c
void* ncclGetLocalPointer(ncclWindow_t w, size_t offset)
```

返回指向 window `w` 内**当前设备**内存 buffer 的 load/store 可访问指针，`offset` 为字节偏移。

这只是一个快捷形式，等价于：

- `ncclGetPeerPointer` 传 `peer = devComm.rank`，或
- `ncclGetLsaPointer` 传 `lsaPeer = devComm.lsaRank`

## Multimem

### ncclGetLsaMultimemPointer

```c
void* ncclGetLsaMultimemPointer(ncclWindow_t w, size_t offset, ncclDevComm const& devComm)
```

返回与 window `w` 和 device communicator `devComm` 关联的 **multicast 内存指针**，`offset` 为字节偏移。

两个限制：

- **multicast 内存的可用性取决于硬件。**
- **当前不支持包含 host-backed segment（`CU_MEM_LOCATION_TYPE_HOST_NUMA`）的内存区域。**

## 三个指针访问器的区别

| 函数 | rank 索引所属 team | 越界/不可达时 |
| --- | --- | --- |
| `ncclGetPeerPointer` | world team | peer 不在 LSA team 内则返回 NULL |
| `ncclGetLsaPointer` | LSA team | — |
| `ncclGetLocalPointer` | 自己（无需传 rank） | — |
