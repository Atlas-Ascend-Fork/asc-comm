# 26 · API · Device API – GIN

> 原文：NCCL API → Device API → Device API – GIN

以下均为 **device 函数，只能从 device（GPU）代码调用**。GIN 自 **NCCL 2.28.7** 起支持。

## ncclGin 类

封装 GIN 支持主要元素的类。

### 构造函数

```c
ncclGin(ncclDevComm const& comm, int contextIndex)
```

- `comm`：用 `ncclDevCommCreate()` 创建的 device communicator。
- `contextIndex`：**GIN context 的索引**——一条网络通信通道。

关于 context 的性能建议：使用多个 GIN context 让实现能把流量分散到多条连接上，避免加锁和瓶颈。因此**追求性能的 kernel 应该在可用 context 之间轮转**以提升资源利用率。可用 context 数量通过 `ginContextCount` 获取（见 `24`）。

**一个 `ncclGin` 对象总是代表一个特定 context。** 若要执行一个 fence 覆盖该 comm 上**所有** GIN context 的 barrier（操作被分片到多个 context 时有用，例如多 NIC 场景），把 `ncclGinAllContexts(comm)` 传给 `ncclGinBarrier()` 来替代 `ncclGin`。

### put

```c
void put(ncclTeam team, int peer, ncclWindow_t dstWnd, size_t dstOffset,
         ncclWindow_t srcWnd, size_t srcOffset, size_t bytes,
         RemoteAction remoteAction, LocalAction localAction, Coop coop,
         DescriptorSmem descriptor, cuda::thread_scope alreadyReleased,
         cuda::thread_scope expected_scope, SegmentType bufType)
```

调度一次 **device 发起的单边数据传输**，从本地 buffer 到 peer 上的远端 buffer。

必需参数：

- `peer` 是 `team` 内的 rank（见 `14` 的 Teams），**可以指向本地 rank（loopback）**。
- 目的和源 buffer 各用 window（`dstWnd`、`srcWnd`）加字节偏移（`dstOffset`、`srcOffset`）指定。
- `bytes` 是传输字节数。
- **约束**：若 GIN 以 `NCCL_GIN_CONNECTION_RAIL` 初始化，`peer` 必须与本地 rank 处于**同一 rail team**。

**前七个参数之后的都是可选的**：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `remoteAction` | `ncclGin_None`（无动作） | 负载落定后在**目标 peer** 上执行的动作。可选如 `ncclGin_Signal{Inc\|Add}` |
| `localAction` | `ncclGin_None` | 输入被消费后在**本地 rank** 上执行的动作。可选如 `ncclGin_CounterInc` |
| `coop` | `ncclCoopThread`（单个 device 线程，**推荐模型**） | 参与本操作的线程集合 |
| `bufType` | `ncclGin_SegmentDevice` | 源和目的 buffer 的物理内存构成（见 `14` 的 Segment Types） |

**可见性语义**：目标 peer 上 signal 的可见性**隐含**其所附带的 put 数据的可见性。取决于 signal 类型，signal 的可见性**还可能**隐含此前所有发往同一 peer、同一 context 的 put 的可见性。

**其他变体**：API 还定义了一个"便捷"变体，用 `ncclSymPtr` 类型指定 buffer，且 size 以**元素个数**而非字节数表达。另有两个 `putValue` 变体，一次按值传入单个元素（不超过 8 字节）。

### get

```c
void get(ncclTeam team, int peer, ncclWindow_t remoteWnd, size_t remoteOffset,
         ncclWindow_t localWnd, size_t localOffset, size_t bytes,
         Coop coop = ncclCoopThread{}, DescriptorSmem descriptor = ncclGin_None{},
         uint32_t optFlags = ncclGinOptFlagsDefault,
         SegmentType bufType = ncclGin_SegmentDevice{})
```

调度一次 device 发起的单边数据传输，**从远端 buffer 到本地 buffer**（**NCCL 2.30.3 起**可用）。

参数语义与 `put` 对应：`peer` 可为 loopback；远端和本地 buffer 各用 window 加字节偏移指定；`bytes` 为字节数；`NCCL_GIN_CONNECTION_RAIL` 下 `peer` 必须同 rail team；`bufType` 默认 `ncclGin_SegmentDevice`。

### flush

```c
void flush(Coop coop, cuda::memory_order ord = cuda::memory_order_acquire)
```

确保 `coop` 中任何线程调度的所有待处理传输操作都已**在本地被消费**。

**注意区分两种操作的含义**：

| 操作 | flush 后的保证 |
| --- | --- |
| `put` | 源 buffer 可以安全复用。**对远端 peer 的完成状态不做任何保证** |
| `get` | 数据对本地 rank 可见 |

### flushAsync

```c
void flushAsync(ncclTeam team, uint32_t peer, ncclGinRequest_t* request,
                Coop coop = ncclCoopThread{},
                uint32_t optFlags = ncclGinOptFlagsDefault,
                DescriptorSmem descriptor = ncclGin_None{})
```

对**一个 peer** 发起非阻塞 flush 操作。`peer` 是 `team` 内的 rank。`request` 由调用方提供、由 `flushAsync` 初始化，调用方可用它判断 flush 何时完成（见 `wait()`）。**NCCL 2.30.3 起**可用。

### wait

```c
void wait(ncclGinRequest_t& request, Coop coop = ncclCoopThread{},
          DescriptorSmem descriptor = ncclGin_None{},
          cuda::memory_order ord = cuda::memory_order_acquire)
```

阻塞直到 `request` 完成。**NCCL 2.30.3 起**可用。

## Signals 与 Counters

### ncclGinSignal_t

Signal 用于**在远端 peer 上触发动作**，最常见的是在 `ncclGin::put()` 完成时触发。每个 signal 关联一个可原子操作的 **64 位整数值**。

**NCCL 2.30.5 起分为两类**：

| 类型 | 可见性含义 |
| --- | --- |
| **Strong** signal | 隐含此前所有发往同一 peer、同一 context 的 put 的可见性 |
| **Weak** signal | 只隐含该 signal 所附带的那次 put 数据的可见性 |

对应的结构体：

```c
struct ncclGin_StrongSignalInc { ncclGinSignal_t signal; };
struct ncclGin_StrongSignalAdd { ncclGinSignal_t signal; uint64_t value; };
struct ncclGin_WeakSignalInc   { ncclGinSignal_t signal; };
struct ncclGin_WeakSignalAdd   { ncclGinSignal_t signal; uint64_t value; };
```

这些对象作为 `ncclGin::put()`、`ncclGin::signal()` 等方法的 `remoteAction` 参数传入，描述 peer 收到时要执行的动作——这里就是按索引递增指定 signal 的值。

两个易踩的点：

- `SignalInc{signalIdx}` 功能上等价于 `SignalAdd{signalIdx, 1}`；但**不经过 signal reset 就不能与其他修改 signal 的操作混用**。
- signal 值使用**"滚动"比较逻辑**，确保无符号溢出时仍保持 `x < x + 1` 的性质。

### VA Signals

```c
struct ncclGin_StrongVASignalInc { ncclWindow_t signalWindow; size_t signalOffset; };
struct ncclGin_StrongVASignalAdd { ncclWindow_t signalWindow; size_t signalOffset; uint64_t value; };
struct ncclGin_WeakVASignalInc   { ncclWindow_t signalWindow; size_t signalOffset; };
struct ncclGin_WeakVASignalAdd   { ncclWindow_t signalWindow; size_t signalOffset; uint64_t value; };
```

"VA signal"指位于**任意 VA（window + offset 组合）**的 signal，而非预分配的 signal 索引。同样作为 `remoteAction` 参数传入以递增 peer 上的 signal。

**两条硬性要求**：

- 使用 VA signal 时，**window 必须以 `NCCL_WIN_STRICT_ORDERING` flag 注册**。
- 当一个地址被用作 signal 时，**对该地址的所有读写都必须通过 GIN 发出**（即通过 RemoteAction 或 GIN signal 方法）。

### 已废弃的 signal 类型

**2.30.5 起废弃**，被显式的 strong/weak 版本取代：

| 已废弃 | 改用 |
| --- | --- |
| `ncclGin_SignalInc` | `ncclGin_StrongSignalInc` 或 `ncclGin_WeakSignalInc` |
| `ncclGin_SignalAdd` | `ncclGin_StrongSignalAdd` 或 `ncclGin_WeakSignalAdd` |
| `ncclGin_VASignalInc` | `ncclGin_StrongVASignalInc` 或 `ncclGin_WeakVASignalInc` |
| `ncclGin_VASignalAdd` | `ncclGin_StrongVASignalAdd` 或 `ncclGin_WeakVASignalAdd` |

这些废弃类型的强弱由创建 device communicator 时 **`ginStrongSignalsRequired` 的值**决定。

### ncclGin 的 signal 方法

```c
void ncclGin::signal(ncclTeam team, int peer, RemoteAction remoteAction, Coop coop,
                     DescriptorSmem descriptor, cuda::thread_scope alreadyReleased,
                     cuda::thread_scope expected_scope)

uint64_t ncclGin::readSignal(ncclGinSignal_t signal, int bits = 64,
                             cuda::memory_order ord = cuda::memory_order_acquire)

void ncclGin::waitSignal(Coop coop, ncclGinSignal_t signal, uint64_t least, int bits = 64,
                         cuda::memory_order ord = cuda::memory_order_acquire)

void ncclGin::resetSignal(ncclGinSignal_t signal)
```

| 方法 | 作用 |
| --- | --- |
| `signal()` | 实现**不带数据传输**的显式 signal 通知，参数是 `put()` 参数的子集 |
| `readSignal()` | 返回 signal 值的**低 `bits` 位** |
| `waitSignal()` | 等待 signal 值的低 `bits` 位**达到或超过 `least`** |
| `resetSignal()` | 把 signal 值重置为 0。**该方法不得与对该 signal 的并发修改竞争** |

VA signal 专用方法（以 window + offset 定位）：

```c
uint64_t ncclGin::readSignal(ncclWindow_t signalWindow, size_t signalOffset, int bits = 64,
                             cuda::memory_order ord = cuda::memory_order_acquire)

void ncclGin::waitSignal(Coop coop, ncclWindow_t signalWindow, size_t signalOffset,
                         uint64_t least, int bits = 64,
                         cuda::memory_order ord = cuda::memory_order_acquire)

void ncclGin::resetSignal(ncclWindow_t signalWindow, size_t signalOffset)
```

### ncclGinCounter_t

Counter 用于**在本地 rank 上触发动作**，因此与面向远端动作的 signal 互补。

- 同样使用"滚动"比较逻辑。
- 但**最多只能存储 56 位的值**（signal 是 64 位）。

```c
struct ncclGin_CounterInc { ncclGinCounter_t counter; };
```

该对象作为 `ncclGin::put()` 等方法的 **`localAction`** 参数传入，是**counter 唯一定义的动作**。

counter 方法（功能上与上面的 signal 对应版本等价，注意默认 `bits` 是 56）：

```c
uint64_t ncclGin::readCounter(ncclGinCounter_t counter, int bits = 56,
                              cuda::memory_order ord = cuda::memory_order_acquire)

void ncclGin::waitCounter(Coop coop, ncclGinCounter_t counter, uint64_t least, int bits = 56,
                          cuda::memory_order ord = cuda::memory_order_acquire)

void ncclGin::resetCounter(ncclGinCounter_t counter)
```

## ncclGinBarrierSession

```c
template<typename Coop>
class ncclGinBarrierSession
```

表示一次**网络 barrier session** 的类。

### 构造函数（rail 专用版）

```c
ncclGinBarrierSession(Coop coop, ncclGin gin, ncclTeamTagRail tag, uint32_t index)
```

- `coop`：协作组。
- `gin`：此前初始化好的 `ncclGin` 对象。
- `ncclTeamTagRail`：表示该 barrier 作用于与本地 rank 同 rail 的所有 peer。
- `index`：标识要使用的底层 barrier，**每个 `coop` 应不同**，通常设为 `blockIdx.x` 保证 CTA 间唯一。

### 构造函数（通用版）

```c
ncclGinBarrierSession(Coop coop, ncclGin gin, ncclTeam team,
                      ncclGinBarrierHandle handle, uint32_t index)
```

通用变体，用于例如与 world team 的 rank 通信的场景（前一个变体专用于 rail team）。

- 需显式传入 `team`。
- 额外的 `handle` 参数指明底层 barrier 的位置，**通常设为 device communicator 的 `railGinBarrier` 字段**。

### sync

```c
void sync(Coop coop, cuda::memory_order order,
          ncclGinFenceLevel fence = ncclGinFenceLevel::Put | ncclGinFenceLevel::Get)
```

同步参与该 barrier session 的所有 team 成员的所有线程。

`fence` 是**位标志枚举**，选择 barrier 返回后哪些先前的网络操作必须已完成。**省略时默认为 `Put | Get`**，因此不显式选择的调用者得到最强保证。

| Fence level | 语义 |
| --- | --- |
| `ncclGinFenceLevel::None` | 纯同步，不做 drain |
| `ncclGinFenceLevel::Put` | barrier 返回后，其他 team 成员在 barrier 之前发出的、以调用方 rank 为目标的 put **在调用方内存中可见** |
| `ncclGinFenceLevel::Get` | barrier 返回后，调用方在 barrier 之前发出的 get **已落地到调用方本地内存** |

这些值是位标志，用**按位或**组合。要同时要求 Put 和 Get 语义就传 `ncclGinFenceLevel::Put | ncclGinFenceLevel::Get`。

**`ncclGinFenceLevel::Relaxed` 作为 `None` 的废弃别名保留**以兼容源码级向后兼容；新代码应使用 `None`。

## 版本速查

| 特性 | 起始版本 |
| --- | --- |
| GIN 基线 | 2.28.7 |
| `get()`、`flushAsync()`、`wait()` | 2.30.3 |
| strong / weak signal 区分 | 2.30.5 |
