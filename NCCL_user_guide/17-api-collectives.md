# 17 · API · 集合通信函数

> 原文：NCCL API → Collective Communication Functions
>
> 语义层面的说明见 `05-collective-operations.md`，本章是函数签名与参数细节。

所有函数返回 `ncclResult_t`，最后一个参数都是 `cudaStream_t stream`。

## ncclAllReduce

```c
ncclResult_t ncclAllReduce(const void* sendbuff, void* recvbuff, size_t count,
                           ncclDataType_t datatype, ncclRedOp_t op,
                           ncclComm_t comm, cudaStream_t stream)
```

用 `op` 归约 `sendbuff` 中长度为 `count` 的数据数组，并在**每个 `recvbuff`** 中留下相同的结果副本。

**in-place 条件**：`sendbuff == recvbuff`

## ncclBroadcast

```c
ncclResult_t ncclBroadcast(const void* sendbuff, void* recvbuff, size_t count,
                           ncclDataType_t datatype, int root,
                           ncclComm_t comm, cudaStream_t stream)
```

把 root rank 上 `sendbuff` 里的 `count` 个元素复制到所有 rank 的 `recvbuff`。

`sendbuff` **只在 rank root 上使用**，其他 rank 忽略该参数。

**in-place 条件**：`sendbuff == recvbuff`

### ncclBcast（legacy）

```c
ncclResult_t ncclBcast(void* buff, size_t count, ncclDataType_t datatype,
                       int root, ncclComm_t comm, cudaStream_t stream)
```

`ncclBroadcast` 的 legacy in-place 版本，风格类似 `MPI_Bcast`。以下两者等价：

```c
ncclBcast(buff, count, datatype, root, comm, stream)
ncclBroadcast(buff, buff, count, datatype, root, comm, stream)
```

## ncclReduce

```c
ncclResult_t ncclReduce(const void* sendbuff, void* recvbuff, size_t count,
                        ncclDataType_t datatype, ncclRedOp_t op, int root,
                        ncclComm_t comm, cudaStream_t stream)
```

用 `op` 把 `sendbuff` 中长度为 `count` 的数据数组归约到 **root rank 的 `recvbuff`**。

`recvbuff` **只在 rank root 上使用**，其他 rank 忽略。

**in-place 条件**：`sendbuff == recvbuff`

## ncclAllGather

```c
ncclResult_t ncclAllGather(const void* sendbuff, void* recvbuff, size_t sendcount,
                           ncclDataType_t datatype, ncclComm_t comm, cudaStream_t stream)
```

从所有 GPU 收集 `sendcount` 个值，在每个 `recvbuff` 中留下相同结果副本；**来自 rank i 的数据放在 offset `i*sendcount` 处**。

**尺寸要求**：假定接收 count 等于 `nranks*sendcount`，即 `recvbuff` 至少要有 `nranks*sendcount` 个元素。

**in-place 条件**：`sendbuff == recvbuff + rank * sendcount`

## ncclReduceScatter

```c
ncclResult_t ncclReduceScatter(const void* sendbuff, void* recvbuff, size_t recvcount,
                               ncclDataType_t datatype, ncclRedOp_t op,
                               ncclComm_t comm, cudaStream_t stream)
```

用 `op` 归约所有 GPU 上 `sendbuff` 中的数据，并把归约结果 scatter 到各设备上，使 **rank i 的 `recvbuff` 含结果的第 i 块**。

**尺寸要求**：假定发送 count 等于 `nranks*recvcount`，即 `sendbuff` 至少要有 `nranks*recvcount` 个元素。

**in-place 条件**：`recvbuff == sendbuff + rank * recvcount`

## ncclAlltoAll

```c
ncclResult_t ncclAlltoAll(const void* sendbuff, void* recvbuff, size_t count,
                          ncclDataType_t datatype, ncclComm_t comm, cudaStream_t stream)
```

每个 rank 向所有其他 rank 发 `count` 个值，也从所有其他 rank 收 `count` 个值。

- 发给目标 rank j 的数据取自 `sendbuff + j*count`
- 从源 rank i 收到的数据放在 `recvbuff + i*count`

**尺寸要求**：假定总发送和总接收 count 都等于 `nranks*count`，即 `sendbuff` 和 `recvbuff` 各至少要有 `nranks*count` 个元素。

**in-place：当前不支持。**

## ncclGather

```c
ncclResult_t ncclGather(const void* sendbuff, void* recvbuff, size_t count,
                        ncclDataType_t datatype, int root,
                        ncclComm_t comm, cudaStream_t stream)
```

每个 rank 从 `sendbuff` 发 `count` 个元素给 root rank。在 root 上，来自 rank i 的数据放在 `recvbuff + i*count`。**非 root rank 不使用 `recvbuff`。**

**尺寸要求**：假定接收 count 等于 `nranks*count`，即 `recvbuff` 至少 `nranks*count` 个元素。

**in-place 条件**：`sendbuff == recvbuff + root * count`

## ncclScatter

```c
ncclResult_t ncclScatter(const void* sendbuff, void* recvbuff, size_t count,
                         ncclDataType_t datatype, int root,
                         ncclComm_t comm, cudaStream_t stream)
```

每个 rank 从 root rank 收 `count` 个元素。在 root 上，`sendbuff + i*count` 处的 `count` 个元素发给 rank i。**非 root rank 不使用 `sendbuff`。**

**尺寸要求**：假定发送 count 等于 `nranks*count`，即 `sendbuff` 至少 `nranks*count` 个元素。

**in-place 条件**：`recvbuff == sendbuff + root * count`

## in-place 条件速查

| 函数 | in-place 条件 |
| --- | --- |
| `ncclAllReduce` | `sendbuff == recvbuff` |
| `ncclBroadcast` | `sendbuff == recvbuff` |
| `ncclReduce` | `sendbuff == recvbuff` |
| `ncclAllGather` | `sendbuff == recvbuff + rank * sendcount` |
| `ncclReduceScatter` | `recvbuff == sendbuff + rank * recvcount` |
| `ncclAlltoAll` | 不支持 |
| `ncclGather` | `sendbuff == recvbuff + root * count` |
| `ncclScatter` | `recvbuff == sendbuff + root * count` |

注意 Gather/Scatter 用的是 **`root`** 而非 `rank`，与 AllGather/ReduceScatter 不同。
