# 16 · API · Communicator 创建与管理

> 原文：NCCL API → Communicator Creation and Management Functions
>
> 用法层面的说明见 `04-creating-a-communicator.md`。

## 错误与版本查询

### ncclGetLastError

```c
const char* ncclGetLastError(ncclComm_t comm)
```

返回 NCCL 中最近一次错误的人可读字符串。

两个易踩的点：

- **调用本函数不会清除该错误。**
- 返回的字符串**可能与当前调用无关**，可能是此前发起的异步操作的结果。

### ncclGetErrorString

```c
const char* ncclGetErrorString(ncclResult_t result)
```

返回与传入错误码对应的人可读字符串。

### ncclGetVersion

```c
ncclResult_t ncclGetVersion(int* version)
```

返回当前链接的 NCCL 库的版本号，编码为一个整数，包含 `NCCL_MAJOR`、`NCCL_MINOR`、`NCCL_PATCH` 三级。

返回值与 `nccl.h` 中定义的 `NCCL_VERSION_CODE` 相同。比较版本可用宏 `NCCL_VERSION(MAJOR,MINOR,PATCH)`。

## 创建 communicator

### ncclGetUniqueId

```c
ncclResult_t ncclGetUniqueId(ncclUniqueId* uniqueId)
```

生成供 `ncclCommInitRank` 使用的 Id。

- 创建 communicator 时**只调一次**，并在调 `ncclCommInitRank` 之前把该 Id 分发给 communicator 内所有 rank。
- `uniqueId` 应指向**由用户分配**的 `ncclUniqueId` 对象。

### ncclCommInitRank

```c
ncclResult_t ncclCommInitRank(ncclComm_t* comm, int nranks, ncclUniqueId commId, int rank)
```

创建一个新 communicator（多线程/多进程版本）。

- `rank` 必须在 0 到 `nranks-1` 之间，且在 communicator clique 内唯一。
- 每个 rank 关联一个 CUDA 设备，**该设备必须在调用本函数之前设好**。
- 本函数**隐式与其他 rank 同步**，因此必须由不同线程/进程调用，或者放在 `ncclGroupStart`/`ncclGroupEnd` 内使用。

### ncclCommInitAll

```c
ncclResult_t ncclCommInitAll(ncclComm_t* comms, int ndev, const int* devlist)
```

以**阻塞方式**创建一组 communicator（单进程版本），是创建单进程 communicator clique 的便捷函数。

- 在 `comms` 中返回 `ndev` 个新初始化的 communicator。
- `comms` 需**预先分配**至少 `ndev*sizeof(ncclComm_t)` 大小。
- `devlist` 定义各 rank 关联的 CUDA 设备；若为 NULL，则按顺序使用前 `ndev` 个 CUDA 设备。

### ncclCommInitRankConfig

```c
ncclResult_t ncclCommInitRankConfig(ncclComm_t* comm, int nranks, ncclUniqueId commId,
                                    int rank, ncclConfig_t* config)
```

与 `ncclCommInitRank` 相同，但额外接受一个配置参数指定 communicator 的附加属性。

`config` 传 NULL 时行为等同于 `ncclCommInitRank`（默认行为）。配置项细节见 `20-api-types.md` 的 `ncclConfig_t`。

### ncclCommInitRankScalable

```c
ncclResult_t ncclCommInitRankScalable(ncclComm_t* newcomm, int nranks, int myrank,
                                      int nId, ncclUniqueId* commIds, ncclConfig_t* config)
```

与 `ncclCommInitRankConfig` 相同，但接受一个 `ncclUniqueId` **列表**而非单个。

- 只传一个 id 时，行为等同于 `ncclCommInitRankConfig`。
- 提供的所有 id **都会被用来初始化参数中给出的那一个 communicator**。

## 派生 communicator

### ncclCommSplit

```c
ncclResult_t ncclCommSplit(ncclComm_t comm, int color, int key,
                           ncclComm_t* newcomm, ncclConfig_t* config)
```

**集合函数**，从已有 communicator 创建一组新 communicator。

| 参数 | 语义 |
| --- | --- |
| `color` | 传入相同 `color` 的 rank 属于同一组；必须是**非负值**。传 `NCCL_SPLIT_NOCOLOR` 表示该 rank 不属于任何组，`newcomm` 返回 NULL |
| `key` | 决定 rank 顺序，**key 越小新 communicator 中的 rank 越小**。key 相同时，按原 communicator 中的 rank 排序 |
| `config` | 需要特殊配置时传入；传 NULL 则新 communicator **继承原 communicator 的配置** |

**split 时 `comm` 上不应有未完成的 NCCL 操作**，否则可能死锁。

### ncclCommShrink

```c
ncclResult_t ncclCommShrink(ncclComm_t comm, int* excludeRanksList, int excludeRanksCount,
                            ncclComm_t* newcomm, ncclConfig_t* config, int shrinkFlags)
```

通过从已有 communicator 移除指定 rank 来创建新 communicator。

- **集合函数**，必须由新建 communicator 中**所有参与 rank** 调用。
- `excludeRanksList` 中的 rank **不应调用本函数**。
- 列在 `excludeRanksList`（大小 `excludeRanksCount`）中的原 rank 会被排除在新 communicator 之外。
- 新 communicator 内 **rank 会被更新以保持连续的 id 集合**。
- `config` 传 NULL 则继承父 communicator 配置。

`shrinkFlags` 的两种行为差异（flag 定义见 `22-api-flags.md`）：

| Flag | 对未完成操作的要求 | 资源共享 |
| --- | --- | --- |
| `NCCL_SHRINK_DEFAULT`（或 `0`） | `comm` 上**不应有未完成操作**，否则可能死锁 | 若父 communicator 设了 `config.shrinkShare = 1`，NCCL 会**复用父 communicator 资源** |
| `NCCL_SHRINK_ABORT` | NCCL 会**自动 abort** 父 communicator 上的未完成操作 | 父与新建 communicator 之间**不共享任何资源** |

### ncclCommGetUniqueId

```c
ncclResult_t ncclCommGetUniqueId(ncclComm_t comm, ncclUniqueId* uniqueId)
```

为**扩展已有 communicator 一次**生成唯一标识符。

- 每次 grow 操作前，**只能由一个 rank（coordinator）调用**。
- coordinator 负责在新 rank 通过 `ncclCommGrow` 加入之前把 `uniqueId` 分发给它们。
- **调用时 communicator 上不应有未完成的 NCCL 操作。**

### ncclCommGrow

```c
ncclResult_t ncclCommGrow(ncclComm_t comm, int nRanks, const ncclUniqueId* uniqueId,
                          int rank, ncclComm_t* newcomm, ncclConfig_t* config)
```

通过向已有 communicator 添加新 rank 来创建新 communicator。**必须由已有 rank（来自父 communicator）和新 rank 双方都调用**，但参数传法不同：

| 参数 | 已有 rank | 新 rank |
| --- | --- | --- |
| `comm` | 父 communicator | `NULL` |
| `rank` | 必须为 **`-1`**（已有 rank 在新 communicator 中保留原 rank） | 新 communicator 中期望的 rank，**必须 ≥ 父 communicator 大小** |
| `uniqueId` | `NULL`（已有 rank 在内部获取协调信息） | 从 coordinator 调 `ncclCommGetUniqueId` 得到的标识符 |

其他：

- `nRanks` 指定新 communicator 的**总 rank 数**，必须**大于**父 communicator 的大小。
- `config` 传 NULL 时：已有 rank 继承父 communicator 配置，新 rank 使用默认配置。
- **调用时父 communicator 上不应有未完成的 NCCL 操作**，否则可能死锁。
- grow 完成后，应用 `ncclCommDestroy` 销毁父 communicator 释放资源。

典型流程：

1. coordinator rank 调 `ncclCommGetUniqueId` 生成 grow 标识符
2. coordinator 通过 out-of-band 方式把 `uniqueId` 分发给所有新 rank
3. 所有已有 rank 调 `ncclCommGrow`，参数 `comm`=父、`rank`=-1、`uniqueId`=NULL（coordinator rank 除外，它传 `uniqueId`）
4. 所有新 rank 调 `ncclCommGrow`，参数 `comm`=NULL、`rank`=新 rank、`uniqueId`=收到的 id

### ncclCommRevoke

```c
ncclResult_t ncclCommRevoke(ncclComm_t comm, int revokeFlags)
```

**撤销 communicator 上进行中的操作，但不销毁资源。**

- 成功返回值可能是 `ncclInProgress`（非阻塞），撤销在后台异步完成；应用可以轮询 `ncclCommGetAsyncError` 直到返回 `ncclSuccess`。
- `revokeFlags` 必须设为 `NCCL_REVOKE_DEFAULT`（0），其他值保留供未来使用。

revoke 完成后的状态与限制：

- communicator 处于 quiesced 状态，**可以安全地 destroy、split 和 shrink**。
- 在已 revoke 的 communicator 上发起新集合操作会返回 **`ncclInvalidUsage`**。
- **revoke 之后调 `ncclCommFinalize` 不受支持。**
- 父 communicator 被 revoke 时，通过 `splitShare` / `shrinkShare` 的资源共享**被禁用**。

## 销毁 communicator

### ncclCommFinalize

```c
ncclResult_t ncclCommFinalize(ncclComm_t comm)
```

Finalize 一个 communicator 对象。

- communicator 标记为 nonblocking 时，本函数是**非阻塞**的。成功返回会把 communicator 状态设为 `ncclInProgress`，表示 communicator 正在 finalize 中——所有未完成操作和网络相关资源正在被 flush 和释放。
- 所有 NCCL 操作完成后，communicator 转为 `ncclSuccess` 状态，可用 `ncclCommGetAsyncError` 查询。

### ncclCommDestroy

```c
ncclResult_t ncclCommDestroy(ncclComm_t comm)
```

销毁一个 communicator 对象。

- 若用户调过 `ncclCommFinalize`，则应保证 communicator 状态变为 `ncclSuccess` 之后再调本函数。
- 任何情况下，**本函数返回后都不得再访问该 communicator**。
- **推荐做法**：先调 `ncclCommFinalize`，再调 `ncclCommDestroy`。
- 本函数**内部会调 `ncclCommFinalize`**，除非此前已对该 communicator 调过。若此前已调过，则本函数是**纯本地操作**。
- **这是一个节点内集合调用**：同一节点上所有 rank 都应调用，否则会 hang。

### ncclCommAbort

```c
ncclResult_t ncclCommAbort(ncclComm_t comm)
```

释放分配给 communicator 对象的资源，并在销毁 communicator 之前 **abort 所有未完成操作**。

**所有活跃 rank 都必须调用本函数**，才能成功 abort 该 NCCL communicator。更多用例见 `04-creating-a-communicator.md` 的容错一节。

## 状态查询

### ncclCommGetAsyncError

```c
ncclResult_t ncclCommGetAsyncError(ncclComm_t comm, ncclResult_t* asyncError)
```

查询异步 NCCL 操作的进度和潜在错误。

按是否带 stream 参数区分：

| 操作类型 | 完成判定 |
| --- | --- |
| 不需要 stream 参数（如 `ncclCommFinalize`） | 函数返回 `ncclSuccess` 即可视为完成 |
| 带 stream 参数（如 `ncclAllReduce`） | 操作被 post 到 stream 上即返回 `ncclSuccess`，但在真正完成之前**仍可能通过本函数报错** |

若任何 NCCL 函数返回 `ncclInProgress`：

- 说明该操作正在后台入队过程中。
- 用户**必须轮询各 communicator 的状态，直到全部变为 `ncclSuccess`，才能调用下一个 NCCL 函数**。
- 在状态变为 `ncclSuccess` 之前，**不允许向 NCCL 正在使用的 stream 发射 CUDA kernel**。

若 communicator 上发生了错误：

- 应用 `ncclCommAbort()` 销毁该 communicator。
- **对于已入队到该 communicator 的操作，其完成状态和正确性都不能做任何假设。**

### ncclCommCount / ncclCommCuDevice / ncclCommUserRank

```c
ncclResult_t ncclCommCount(const ncclComm_t comm, int* count)
ncclResult_t ncclCommCuDevice(const ncclComm_t comm, int* device)
ncclResult_t ncclCommUserRank(const ncclComm_t comm, int* rank)
```

分别返回：communicator 中的 rank 数、communicator 关联的 CUDA 设备、调用者在 communicator 中的 rank。

## 内存注册

### ncclCommRegister / ncclCommDeregister

```c
ncclResult_t ncclCommRegister(const ncclComm_t comm, void* buff, size_t size, void** handle)
ncclResult_t ncclCommDeregister(const ncclComm_t comm, void* handle)
```

在 communicator 下注册 buffer 用于**零拷贝通信**，返回 `handle` 供之后解注册。`buff` 和 `size` 的要求见 `13-user-buffer-registration.md`。

### ncclCommWindowRegister

```c
ncclResult_t ncclCommWindowRegister(ncclComm_t comm, void* buff, size_t size,
                                    ncclWindow_t* win, int winFlags)
```

把本地 buffer **集合式地**注册进 NCCL window。

- 这是**集合调用**，communicator 内**每个 rank 都需参与**注册。
- **各 rank 的 size 可以不同**；调用方有责任确保后续操作只访问对参与该操作的各 rank 都有效的范围。
- `win` 返回供之后解注册用。**若在 group 内调用，该值可能要到 `ncclGroupEnd()` 完成后才被填入。**
- `winFlags` 控制注册行为，见 `22-api-flags.md`。
- **Host API 不接受被对称注册多次的 buffer**，传入这样的 buffer 会导致未定义行为。

### ncclCommWindowDeregister

```c
ncclResult_t ncclCommWindowDeregister(ncclComm_t comm, ncclWindow_t win)
```

解注册 NCCL window。

- 解注册是**本 rank 本地的操作**。
- 调用方需确保 window 内对应的 buffer **没有被任何 NCCL 操作访问**。

## 内存分配

### ncclMemAlloc / ncclMemFree

```c
ncclResult_t ncclMemAlloc(void** ptr, size_t size)
ncclResult_t ncclMemFree(void* ptr)
```

分配指定大小的 GPU buffer，头地址由 `ptr` 返回。

**注意**：由于各类 NCCL 优化对 buffer granularity 的要求，**实际分配的大小可能大于请求的大小**。

## 挂起 / 恢复 / 内存统计

### ncclCommSuspend

```c
ncclResult_t ncclCommSuspend(ncclComm_t comm, int flags)
```

挂起 communicator 操作以释放资源。

- **挂起期间该 communicator 不能用于任何 NCCL 操作。**
- 调用时 `comm` 上**不应有未完成的 NCCL 操作**。

`flags` 控制释放哪些资源：

| Flag | 值 | 释放内容 |
| --- | --- | --- |
| `NCCL_SUSPEND_MEM` | `0x01` | 释放该 communicator 持有的动态 GPU 内存分配 |

挂起的 communicator 可通过 `ncclCommResume` 恢复为活跃状态。

### ncclCommResume

```c
ncclResult_t ncclCommResume(ncclComm_t comm)
```

恢复 communicator 上此前被挂起的所有资源。成功返回后，communicator 完全可用，可再次用于 NCCL 操作。

### ncclCommMemStats

```c
ncclResult_t ncclCommMemStats(ncclComm_t comm, ncclCommMemStat_t stat, uint64_t* value)
```

查询 communicator 内存统计。`stat` 选择要获取哪项统计，结果写入 `*value`。可用统计项见 `20-api-types.md` 的 `ncclCommMemStat_t`。
