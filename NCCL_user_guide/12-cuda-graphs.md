# 12 · Using NCCL with CUDA Graphs

> 原文：Using NCCL → Using NCCL with CUDA Graphs

## 支持情况

NCCL **2.9 起**，NCCL 操作可以被 CUDA Graph capture。

CUDA Graph 把工作流定义成图而非单个操作，通过一次 CPU 操作启动多个 GPU 操作来降低开销。细节见 CUDA Programming Guide。

NCCL 的**集合操作、P2P 操作和 group 操作都支持** CUDA Graph capture。该支持要求 **CUDA 最低版本 11.3**。

## 要求与限制

### 单进程多 GPU 可能死锁

**每进程使用多个 GPU 并做 graph capture 可能导致死锁。** 单线程应用尤其容易发生：某些情况下 `cudaGraphLaunch` 会阻塞，导致无法在所有 GPU 上完成 launch。

**推荐每进程单 GPU**，这样最可靠。

### capture 与否是集合属性

一个操作的 launch 是否被 graph capture，被视为该操作的**集合属性**，因此必须在参与该 launch 的**所有 rank 上一致**：

- 集合操作 → communicator 内所有 rank
- 点对点 → 发送方和接收方双方

含有被 capture 的 NCCL 操作的 graph，其 launch（通过 `cudaGraphLaunch` 等）对于**capture 时在场的那同一组 rank** 而言也是集合性的，且这些 rank 每一个都必须使用**源自那次集合 capture 的 graph**。

## 示例：把计算 kernel 和 NCCL 操作一起 capture

```c
cudaGraph_t graph;
cudaStreamBeginCapture(stream);
kernel_A<<< ..., stream >>>(...);
kernel_B<<< ..., stream >>>(...);
ncclAllreduce(..., stream);
kernel_C<<< ..., stream >>>(...);
cudaStreamEndCapture(stream, &graph);

cudaGraphExec_t instance;
cudaGraphInstantiate(&instance, graph, NULL, NULL, 0);
cudaGraphLaunch(instance, stream);
cudaStreamSynchronize(stream);
```

## User buffer 注册带来的额外收益

NCCL **2.11 起**，当 NCCL 通信被 capture 且使用 CollNet 算法时，NCCL 支持通过 **user buffer 注册**进一步提升性能。详见环境变量 `NCCL_GRAPH_REGISTER`。

## 混用 graph-captured 与非 captured 操作

NCCL **支持**混用被 graph capture 的和未被 capture 的 NCCL 操作。

但要注意：当涉及**多个 communicator** 的 graph 从**同一线程** `cudaGraphLaunch` 时，NCCL 用来支持这种混用的内部机制**可能诱发上面提到的死锁**。要关闭该机制，见环境变量 `NCCL_GRAPH_MIXING_SUPPORT`。

## 不支持的组合

**关闭 NCCL 的 capture-time 通信 kernel 序列化**（见 `NCCL_GRAPH_STREAM_ORDERING` 以及 `ncclConfig_t` 中的 `graphStreamOrdering`）**同时启用 graph mixing**（communicator `graphUsageMode=2`）—— 这个组合**不受支持**。

规则：

- 如果某个 communicator 关闭了 ordering，则 graph mixing 必须关闭（`graphUsageMode` 为 0 或 1）。
- 需要 mixing 的负载必须保持**默认 ordering**（值为 `1`）。

## 速查

| 项 | 值/要求 |
| --- | --- |
| NCCL 最低版本 | 2.9（user buffer 注册需 2.11） |
| CUDA 最低版本 | 11.3 |
| 支持的操作 | collective、P2P、group |
| 推荐部署形态 | 每进程单 GPU |
| capture 一致性 | 所有参与 rank 必须一致 capture / 一致不 capture |
| 相关环境变量 | `NCCL_GRAPH_REGISTER`、`NCCL_GRAPH_MIXING_SUPPORT`、`NCCL_GRAPH_STREAM_ORDERING` |
