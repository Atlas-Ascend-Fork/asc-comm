# NVSHMEM 与 CUDA 模型（中文整理）

> 来源：NVSHMEM 2.4.1 文档 · NVSHMEM and the CUDA Model
> https://docs.nvidia.com/nvshmem/archives/nvshmem-241/api/docs/cuda-interactions.html

本页讨论 CUDA 抽象机模型与 NVSHMEM 之间的交互。核心主题是：NVSHMEM 引入的依赖对 CUDA 层不可见，因此哪些用法会死锁、怎么规避。

## 1. CUDA 执行模型

### 1.1 CUDA 中的工作提交

- 所有 CUDA 任务都通过 CUDA stream 提交给 GPU，stream 内按 FIFO 顺序执行。要并发就创建多个 stream 分别入队；跨 stream 依赖用 CUDA event 表达（一个 stream 上 record，另一个 stream 上 wait）。
- NVSHMEM 的 on-stream 操作（如 `nvshmemx_putmem_on_stream`）只是把操作**入队后立即返回**，真正执行要等它排到队首、被 CUDA runtime 执行。
- 所有 on-stream 操作遵循 NVSHMEM 内存模型：用 `nvshmemx_fence_on_stream` 定序，用 `nvshmemx_quiet_on_stream` 完成操作。
- 设备端 API 可由 GPU 上运行的 kernel 调用。由于 kernel launch 也是入队到指定 stream（未指定则为默认 stream），使用设备端发起的 NVSHMEM 操作时必须同时考虑 **CUDA stream 语义** 和 **CUDA 线程执行模型** 两方面的交互。

### 1.2 CUDA 抽象机与"假依赖"

- 除用户通过 stream/event 显式指定的依赖外，CUDA 抽象机模型允许 CUDA 自行插入**假依赖（false dependency）**，前提是不给任务图引入环。这让 CUDA 层可以在共享资源之间调度任务。
- 典型来源：入队到 CUDA stream 的任务，最终通过分配给该 CUDA context 的**有限数量的硬件 stream** 提交给 GPU；硬件 stream 同样 FIFO。当 CUDA 层把多个 CUDA stream 的任务塞进同一个硬件 stream 时，就在它们之间引入了假依赖。
- **风险点**：NVSHMEM 操作本身也会引入依赖（kernel 间点对点同步、on-stream 集合操作需要多 PE 参与），但这些依赖对 CUDA 层**不可见**。CUDA 插的假依赖叠加 NVSHMEM 的真依赖，就可能在执行图中构成环 → 死锁。

## 2. 非本地（nonlocal）操作与 CUDA 执行模型

**定义**：可能一直阻塞、直到另外一个或多个操作被执行才返回的 NVSHMEM 操作，称为 nonlocal。

典型例子：

- 点对点同步操作：阻塞直到本地或远端 PE 的某个 NVSHMEM 操作更新同步变量、满足等待条件。
- 集合操作：阻塞直到 team 内所有 PE 都发出匹配调用。

两种形式的行为差异：

| 形式 | 阻塞效果 |
| --- | --- |
| 基于 stream 的 nonlocal 操作 | 阻塞所在 CUDA stream 以及依赖它的 stream 上的后续任务，直到该操作完成 |
| kernel 发起的 nonlocal 操作 | 既**占用 CUDA 执行资源**，又阻塞它被入队的那个 stream |

### 2.1 场景一：stream 上的真循环依赖

用户必须确保入队到 stream 的 nonlocal 操作不构成循环依赖。例（最左为队首）：

```
PE 0: [ nvshmemx_barrier_all_on_stream, nvshmemx_putmem_signal_on_stream ]
PE 1: [ nvshmemx_signal_wait_until_on_stream, nvshmemx_barrier_all_on_stream ]
```

PE 1 的 signal 等待本应由 PE 0 的 putmem-with-signal 满足，但 PE 0 卡在 barrier 上等 PE 1，无法执行 putmem-with-signal。两边都无法前进 → 死锁。

### 2.2 场景二：stream 顺序 vs 执行资源

`notify_kernel` 调 `nvshmem_putmem_signal` 发消息，`wait_kernel` 调 `nvshmem_signal_wait_until` 等消息，分别入队到不同 stream：

```
PE 0, Stream A: [ wait_kernel ]      PE 0, Stream B: [ notify_kernel ]
PE 1, Stream A: [ wait_kernel ]      PE 1, Stream B: [ notify_kernel ]
```

用户这么写是希望两者并行，但因为在不同 stream 上，CUDA runtime 可以任意顺序执行：

- 若先执行 `wait_kernel`，它会阻塞并**占住执行资源**，而这些资源可能正是 `notify_kernel` 需要的 → notify 上不去。
- 即使资源充足，CUDA runtime 也可能在 kernel 之间插入设备级同步，使两者无法并行。
- 两个 PE 都先执行 `wait_kernel` → 死锁。

**规避**：把两个 kernel 放在同一 stream 且 notify 在前；或插入 CUDA event 防止 `wait_kernel` 被先执行。

### 2.3 场景三：假循环依赖

CUDA runtime 把各 stream 的工作推入 GPU 工作队列（队列内 FIFO），分派时只尊重 stream 顺序依赖和 CUDA event 依赖。NVSHMEM 的 nonlocal 依赖它看不到，所以同样的例子可能被串行化成：

```
PE 0, GPU Work Queue: [ wait_kernel, notify_kernel ]
PE 1, GPU Work Queue: [ wait_kernel, notify_kernel ]
```

→ 死锁。必须引入一个 **CUDA 可见的依赖**，办法是放同一 stream，或用 event：

```
PE 0, Stream A: [ cudaEventSynchronize(e), wait_kernel ]
PE 0, Stream B: [ notify_kernel, cudaEventRecord(e) ]
PE 1, Stream A: [ cudaEventSynchronize(e), wait_kernel ]
PE 1, Stream B: [ notify_kernel, cudaEventRecord(e) ]
```

## 3. Kernel 内同步

CUDA 吞吐量模型允许 grid / thread block 维度远大于 GPU 实际可并行执行的规模，暴露大量工作有利于 GPU 高效处理。但 **CUDA 不保证抢占式线程调度**：kernel 中某个线程执行 nonlocal 操作阻塞时，可能挡住同一 kernel 内其他线程，而被挡住的线程可能正是应该发出匹配调用的那个 → 死锁。

使用 `cudaLaunchCooperativeKernel` 启动 kernel 可避免该问题，它保证 kernel 内线程可以安全地相互同步。

### 3.1 用 NVSHMEM 协同 launch 保证 nonlocal 操作安全

NVSHMEM 提供了专门的 *Kernel Launch Routines*：

- 当 kernel 内部使用了 NVSHMEM 同步或集合 API（`nvshmem_wait`、`nvshmem_barrier`、`nvshmem_barrier_all` 或任何其他集合操作）时，用 `nvshmemx_collective_launch` 启动。
- 不使用同步类 NVSHMEM API、或完全不用 NVSHMEM API 的 kernel，**不要求**用该 API 启动。
- 该调用在 NVSHMEM job 的所有 PE 之间是 collective 的，同时保证：kernel 在每个 PE 上都装得进 GPU，且各 PE 上的 kernel 同时启动。

## 4. 隐式异步的 cudaMemcpy

`cudaMemcpy` / `cudaMemset` 在某些条件下会表现为异步。异步执行时，它们可能在后续 NVSHMEM 操作执行前尚未完成。

- 非 VMM 场景：NVSHMEM 会为对称内存设置 `CU_POINTER_ATTRIBUTE_SYNC_MEMOPS`，强制这类操作同步执行。
- **但该标志目前不支持 VMM 分配**，因此存在数据竞争风险。
- 规避：改用显式异步的 `cudaMemcpyAsync` / `cudaMemsetAsync`，并同步对应 stream。

存在竞争的写法：

```c
cudaMemcpy(out_d, &out, sizeof(unsigned int), cudaMemcpyHostToDevice);
nvshmem_uint_or_reduce(NVSHMEM_TEAM_WORLD, final_out_d, out_d, 1);
```

修正后：

```c
cudaMemcpyAsync(out_d, &out, sizeof(unsigned int), cudaMemcpyHostToDevice, stream);
cudaStreamSynchronize(stream);
nvshmem_uint_or_reduce(NVSHMEM_TEAM_WORLD, final_out_d, out_d, 1);
```

## 要点速查

1. on-stream 操作是入队语义，不是立即执行。
2. nonlocal 操作会阻塞 stream；kernel 内的 nonlocal 还会占住执行资源。
3. NVSHMEM 的依赖 CUDA 看不见 → 需要人为提供 CUDA 可见依赖（同 stream 或 event）。
4. notify 必须能先于 wait 被调度。
5. kernel 内用同步/集合 API → 用 `nvshmemx_collective_launch`。
6. 对称内存的 H2D 拷贝后接 NVSHMEM 操作 → 用 Async 版本 + stream 同步。
