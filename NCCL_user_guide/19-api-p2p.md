# 19 · API · 点对点通信函数

> 原文：NCCL API → Point To Point Communication Functions
>
> 用法与模式示例见 `09-point-to-point.md`。

NCCL 提供两类点对点通信原语：**双边操作**和**单边操作**。

## 双边点对点操作（NCCL 2.7 起）

适用场景：rank 之间需要收发**任意数据**，且无法用 broadcast 或 allgather 表达——也就是所有收发的数据都不相同。**收发双方都必须显式参与。**

### ncclSend

```c
ncclResult_t ncclSend(const void* sendbuff, size_t count, ncclDataType_t datatype,
                      int peer, ncclComm_t comm, cudaStream_t stream)
```

把 `sendbuff` 中的数据发给 rank `peer`。

- rank `peer` 需要用**相同的 datatype 和相同的 count** 调用 `ncclRecv`。
- **该操作对 GPU 而言是阻塞的。** 若多个 `ncclSend()` / `ncclRecv()` 操作需要并发推进才能完成，它们必须被融合在一个 `ncclGroupStart()` / `ncclGroupEnd()` 区间内。

### ncclRecv

```c
ncclResult_t ncclRecv(void* recvbuff, size_t count, ncclDataType_t datatype,
                      int peer, ncclComm_t comm, cudaStream_t stream)
```

从 rank `peer` 接收数据到 `recvbuff`。

- rank `peer` 需要用相同的 datatype 和 count 调用 `ncclSend`。
- 同样**对 GPU 阻塞**，并发推进的多个操作必须放进同一个 group。

## 单边点对点操作（RMA）

单边 RMA 操作让 rank **直接访问远端内存，无需目标进程显式参与**。

**前提**：目标内存必须**预先用 `ncclCommWindowRegister()` 注册**到对称内存 window 中。

### ncclPutSignal

```c
ncclResult_t ncclPutSignal(const void* localbuff, size_t count, ncclDataType_t datatype,
                           int peer, ncclWindow_t peerWin, size_t peerWinOffset,
                           int sigIdx, int ctx, unsigned int flags,
                           ncclComm_t comm, cudaStream_t stream)
```

把 `localbuff` 的数据写入 rank `peer` 已注册内存 window `peerWin` 的 `peerWinOffset` 偏移处，**随后更新一个远端 signal**。

参数约束（当前版本）：

| 参数 | 约束 |
| --- | --- |
| `peerWin` | 必须已用 `ncclCommWindowRegister()` 注册 |
| `sigIdx` | signal 索引标识符，**当前必须设为 0** |
| `ctx` | context 标识符，**当前必须设为 0** |
| `flags` | 保留供未来使用，**当前必须设为 0** |

完成语义（三个层次，要分清）：

1. **函数返回到 CPU 线程** → 表示操作已成功入队到 CUDA stream。
2. **该操作在 CUDA stream 上完成** → `localbuff` 可以安全复用或修改。
3. **远端 peer 上 signal 被更新** → 保证对应 `ncclPutSignal()` 的数据**已交付到远端内存**；并且**面向同一 peer 和同一 context 的所有先前 `ncclPutSignal()` 和 `ncclSignal()` 操作也都已完成各自的 signal 更新**。

### ncclSignal

```c
ncclResult_t ncclSignal(int peer, int sigIdx, int ctx, unsigned int flags,
                        ncclComm_t comm, cudaStream_t stream)
```

向 rank `peer` 发一个 signal，**不传输数据**。

`sigIdx`、`ctx`、`flags` 的约束同上，当前都必须为 0。

远端 peer 上 signal 被更新时，同样保证**面向同一 peer 和同一 context 的所有先前 `ncclPutSignal()` 和 `ncclSignal()` 操作也都已完成 signal 更新**。

### ncclWaitSignal

先看描述符类型：

```c
type ncclWaitSignalDesc_t
```

用于指定：要从某个特定 rank、在给定 signal 索引和 context 上，等待多少次 signal 操作。字段：

| 字段 | 含义 |
| --- | --- |
| `int opCnt` | 要等待的 signal 操作次数 |
| `int peer` | 从哪个 peer 等 signal |
| `int sigIdx` | signal 索引标识符，**当前必须设为 0** |
| `int ctx` | context 标识符，**当前必须设为 0** |

```c
ncclResult_t ncclWaitSignal(int nDesc, ncclWaitSignalDesc_t* signalDescs,
                            ncclComm_t comm, cudaStream_t stream)
```

按 signal 描述符数组的描述等待 signal。

- `nDesc` 指定 `signalDescs` 数组中描述符的个数。
- 每个描述符指明：期望从特定 `peer`、在特定 `sigIdx` 和 `ctx` 上收到多少个（`opCnt`）signal。

完成语义：

1. **函数返回到 CPU 线程** → 操作已成功入队到 CUDA stream。
2. **该操作在 CUDA stream 上完成** → 所有指定的 signal 操作都已收到，**且对应数据在本地内存中可见**。

## 对比速查

| | `ncclSend`/`ncclRecv` | `ncclPutSignal`/`ncclSignal`/`ncclWaitSignal` |
| --- | --- | --- |
| 起始版本 | 2.7 | 2.29（见 `09`） |
| 对端参与 | 必须配对调用 | 不需要 |
| 目标内存 | 普通指针 | 必须注册为 window |
| GPU 阻塞性 | 阻塞，并发需放同一 group | 入队即返回，完成靠 signal |
| 当前限制 | — | `sigIdx`/`ctx`/`flags` 都必须为 0 |
