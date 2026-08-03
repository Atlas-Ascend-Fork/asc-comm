# 30 · 从 NCCL 1 迁移到 NCCL 2

> 原文：Migrating from NCCL 1 to NCCL 2

NCCL 2.x 支持 1.x 所有的集合操作，但 API 有小幅改动。另外，**当单个线程为多个 GPU 管理 NCCL 调用时，2.x 要求使用 Group API**。

以下总结从 1.x 移植到 2.x 时（针对单线程管理多 GPU 的应用）可能需要的改动。

## 初始化

1.x 的两种初始化方式：单线程调 `ncclCommInitAll`，或每 GPU 一个线程并发调 `ncclCommInitRank`。**这两种方式 2.x 都保留。**

2.x 新增了一种基于 Group API 的方式：`ncclCommInitRank` 可以像通信调用一样放在循环里，**但循环必须被 Group start/end 包住**：

```c
ncclGroupStart();
for (int i=0; i<ngpus; i++) {
  cudaSetDevice(i);
  ncclCommInitRank(comms+i, ngpus, id, i);
}
ncclGroupEnd();
```

## 通信

2.x 中同样可以在单线程上用循环为不同设备发起集合操作，与 1.x 类似。两点差异：

- **循环必须被 Group API 包住。**
- **不再需要在通信 API 调用前选择相应的 CUDA 设备**——NCCL runtime 内部会根据 communicator handle 选择关联设备。

```c
ncclGroupStart();
for (int i=0; i<nLocalDevs; i++) {
  ncclAllReduce(..., comm[i], stream[i]);
}
ncclGroupEnd();
```

**每线程单设备或每进程单设备**的场景，API 的一般用法从 1.x 到 2.x 没有变化，**这种情况不需要 group API**。

## Counts 类型

作为参数传入的 count 现在是 **`size_t`** 类型，不再是 `int`。

## AllGather 和 ReduceScatter 的 in-place 用法

详见 `11-inplace-operations.md`。

## AllGather 参数顺序

`ncclAllGather` 的参数顺序改了。原型从：

```c
ncclResult_t ncclAllGather(const void* sendbuff, int count, ncclDataType_t datatype,
                           void* recvbuff, ncclComm_t comm, cudaStream_t stream);
```

改为：

```c
ncclResult_t ncclAllGather(const void* sendbuff, void* recvbuff, size_t sendcount,
                           ncclDataType_t datatype, ncclComm_t comm, cudaStream_t stream);
```

`recvbuff` 被移到 `sendbuff` 之后，以便与其他所有操作保持一致。

这是最容易踩的迁移点：**参数个数没变、类型也大多兼容，编译可能不报错但行为完全错误。**

## 数据类型

2.x 新增了数据类型。**1.x 中已有的类型没有变化，在 2.x 中仍然可用。**

## 错误码

- 错误码被合并进 **`ncclInvalidArgument`** 类别并做了简化。
- 新增 **`ncclInvalidUsage`** 码，用于覆盖新的编程错误。

完整错误码表见 `20-api-types.md`，处理方式对照见 `04-creating-a-communicator.md`。

## 迁移检查清单

| 改动点 | 需要做什么 |
| --- | --- |
| 单线程多 GPU 的 init 循环 | 用 `ncclGroupStart/End` 包住 |
| 单线程多 GPU 的通信循环 | 用 `ncclGroupStart/End` 包住；可以去掉循环内的 `cudaSetDevice` |
| `ncclAllGather` 调用 | **调整参数顺序**，`recvbuff` 移到第二位 |
| count 参数 | 改为 `size_t` |
| 错误处理代码 | 适配合并后的错误码，处理新增的 `ncclInvalidUsage` |
| 每线程/每进程单设备的代码 | 无需改动 |
