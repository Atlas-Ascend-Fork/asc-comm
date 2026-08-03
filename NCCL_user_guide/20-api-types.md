# 20 · API · Types

> 原文：NCCL API → Types

## ncclComm_t

NCCL communicator，指向 NCCL 内部的一个不透明结构。

## ncclResult_t

所有 NCCL 函数的返回值类型。

| 值 | 码 | 含义 |
| --- | --- | --- |
| `ncclSuccess` | 0 | 函数成功 |
| `ncclUnhandledCudaError` | 1 | 对 CUDA 函数的调用失败 |
| `ncclSystemError` | 2 | 对系统的调用失败 |
| `ncclInternalError` | 3 | 内部检查失败。原因是 NCCL 的 bug 或内存损坏 |
| `ncclInvalidArgument` | 4 | 某个参数的值非法 |
| `ncclInvalidUsage` | 5 | 对 NCCL 的调用方式不正确，通常反映编程错误 |
| `ncclRemoteError` | 6 | 调用失败，可能因网络错误或远端进程提前退出 |
| `ncclInProgress` | 7 | communicator 上的某个 NCCL 操作正在入队并在后台推进 |

只要函数返回错误（既非 `ncclSuccess` 也非 `ncclInProgress`），在 `NCCL_DEBUG=WARN` 时 NCCL 应打印更详细的消息。

错误的处理方式对照表见 `04-creating-a-communicator.md`。

## ncclDataType_t

整数类型：

| 类型 | 说明 |
| --- | --- |
| `ncclInt8` / `ncclChar` | 有符号 8 位整数 |
| `ncclUint8` | 无符号 8 位整数 |
| `ncclInt32` / `ncclInt` | 有符号 32 位整数 |
| `ncclUint32` | 无符号 32 位整数 |
| `ncclInt64` | 有符号 64 位整数 |
| `ncclUint64` | 无符号 64 位整数 |

浮点类型：

| 类型 | 说明 |
| --- | --- |
| `ncclFloat16` / `ncclHalf` | 16 位浮点（半精度） |
| `ncclFloat32` / `ncclFloat` | 32 位浮点（单精度） |
| `ncclFloat64` / `ncclDouble` | 64 位浮点（双精度） |
| `ncclBfloat16` | 16 位浮点，bfloat16 格式（截断精度），需 **CUDA 11 或更高** |
| `ncclFloat8e4m3` | 8 位浮点，4 位指数 3 位尾数，需 **CUDA ≥ 11.8 且 SM ≥ 90** |
| `ncclFloat8e5m2` | 8 位浮点，5 位指数 2 位尾数，需 **CUDA ≥ 11.8 且 SM ≥ 90** |

## ncclRedOp_t

定义归约操作：

| 值 | 操作 |
| --- | --- |
| `ncclSum` | 求和（+） |
| `ncclProd` | 求积（*） |
| `ncclMin` | 取最小 |
| `ncclMax` | 取最大 |
| `ncclAvg` | 求平均，即所有 rank 求和后除以 rank 数 |

自定义算子（PreMulSum）见 `21-api-user-defined-ops.md`。

## ncclScalarResidence_t

指明 scalar 参数**驻留在哪个内存空间**、以及**何时可以被解引用**：

| 值 | 含义 |
| --- | --- |
| `ncclScalarHostImmediate` | scalar 驻留在 host 内存，应以最即时的方式解引用 |
| `ncclScalarDevice` | scalar 驻留在 device 可见内存，需要时才解引用 |

## ncclConfig_t

用于初始化 communicator 的结构体式配置。**新建的配置必须用 `NCCL_CONFIG_INITIALIZER` 初始化。**

### NCCL_CONFIG_INITIALIZER

配置宏初始化器，必须赋给新建的配置对象。

### 字段

| 字段 | 取值 / 默认 | 说明 |
| --- | --- | --- |
| `blocking` | 0 或 1，默认 **1（blocking）** | 0 表示非阻塞行为，1 表示阻塞行为 |
| `cgaClusterSize` | 0–8。**sm90 及以后默认 4**，更老架构默认 0 | NCCL 启动的 kernel 的 Cooperative Group Array（CGA）大小 |
| `minCTAs` | 正整数，最大 32，默认 **1** | NCCL 每个 kernel 应使用的**最小** CTA 数 |
| `maxCTAs` | 正整数，最大 32，默认 **32** | NCCL 每个 kernel 应使用的**最大** CTA 数 |
| `netName` | 默认未定义（NCCL 自动选择） | 指定网络模块名，必须与模块名精确匹配（大小写不敏感）。NCCL 内置模块名为 `"IB"`（通用 IB verbs）和 `"Socket"`（TCP/IP sockets），外部网络插件自定义名称 |
| `splitShare` | 0 或 1，默认 **0** | split 时是否与子 communicator 共享资源，详见下文 |
| `shrinkShare` | 0 或 1，默认 **0** | shrink 时是否与子 communicator 共享资源，详见下文 |
| `trafficClass` | ≥ 0 | 该 communicator 网络操作使用的 traffic class，详见下文 |
| `collnetEnable` | 1/0，默认 **0（禁用）** | 是否在该 communicator 上启用 IB SHARP |
| `CTAPolicy` | 默认 `NCCL_CTA_POLICY_DEFAULT` | communicator 的 CTA 策略，全部取值见 `22-api-flags.md` |
| `nvlsCTAs` | 正整数。默认 NCCL 按系统配置自动决定 | NVLS kernel 应使用的 CTA 总数 |
| `commName` | — | 用户自定义的 communicator 名称，NCCL 可用它丰富日志和 profiling 信息 |
| `nChannelsPerNetPeer` | 正整数，会**向上取整到 2 的幂**。默认值针对 AlltoAll 模式优化 | 用于成对通信的网络 channel 数。想提升 send/recv 带宽可以考虑增大 |
| `graphUsageMode` | 0 / 1 / 2，默认 **2** | 0 = 不用 graph，1 = 单个 graph，2 = 多个 graph 或 graph 与非 graph 混用。详见下文约束 |
| `graphStreamOrdering` | 0 / 1，默认 `NCCL_CONFIG_UNDEF_INT`（继承环境变量） | 2.30 起。对 `NCCL_GRAPH_STREAM_ORDERING` 的**按 communicator 覆盖**，详见下文 |
| `maxP2pPeers` | 小于 1 或大于 rank 数时回退为 communicator 的 rank 数 | 任一 rank 用 P2P 并发通信的最大 peer 数。会影响所有 send/recv 及基于 send/recv 的集合操作（all-to-all、scatter、gather） |

### splitShare 详解

父 communicator 在 `ncclCommInitRankConfig` 时以 `splitShare=1` 创建后，split 出的子 communicator 可以共享父的内部资源。split 出的 communicator 属于**同一个 family**。

**关键风险**：资源共享时，**abort 任何一个 communicator 都可能导致同 family 内其他 communicator 变得不可用**。

无论是否共享资源，用户都应始终 abort/destroy 不再需要的 communicator 以释放资源。

**注意**：父 communicator 已被 revoke 时，无论该 flag 如何设置，split 期间的资源共享都被禁用。

### shrinkShare 详解

行为与 `splitShare` 类似。两点例外：

- shrink 配合 `NCCL_SHRINK_ABORT` 使用时，**`shrinkShare` 的值被忽略**，不共享任何资源。
- 父 communicator 已被 revoke 时，资源共享同样被禁用。

### trafficClass 详解

TC 的含义取决于 communicator 所用的网络插件（IB 网络用 service level，RoCE 网络用 type of service）。给不同 communicator 分配不同 TC 对有通信 overlap 的负载有益。TC 由系统配置定义，应 ≥ 0。

**优先级陷阱**：环境变量 `NCCL_IB_SL` 和 `NCCL_IB_TC` **优先于**用户指定的 TC 值。要用自定义 TC，必须确保这些环境变量未设置。

### graphUsageMode 与 graphStreamOrdering 的约束

若 `NCCL_GRAPH_STREAM_ORDERING` 或 `graphStreamOrdering` 关闭了 capture 时的 stream 定序（值为 `0`），则 **graph mixing 必须关闭**——只能用 `graphUsageMode` 0 或 1。**`graphUsageMode=2` 不得与 ordering `0` 组合。**

`graphStreamOrdering` 的两个取值：

- `1`：保持 NCCL 默认的 capture 时通信 kernel 序列化。
- `0`：对该 communicator 关闭该序列化——kernel 被放在 capture stream 上，**由应用自己保证正确定序**。

**同一 GPU 上混用不同值时**：设为 `1` 的 communicator 仍会得到 NCCL 对自身 kernel 的内部序列化，但 NCCL **不会**在它与设为 `0` 的 peer 之间插入跨 communicator 的定序——它们的 kernel 可能在 NCCL 本来会序列化的场景下并发。只有当应用能保证 GPU 上所有可能并发运行的 NCCL 通信 kernel 的顺序时，才使用 `0`。

## ncclSimInfo_t

供 `ncclGroupSimulateEnd()` 返回调用信息的结构体。

- **`NCCL_SIM_INFO_INITIALIZER`**：必须赋给新建的 `ncclSimInfo_t` 结构的配置宏初始化器。
- **`estimatedTime`**：返回该 group 调用中操作的预估时间。

## ncclCommMemStat_t

`ncclCommMemStats()` 的内存统计选择器：

| 选择器 | 返回内容 |
| --- | --- |
| `ncclStatGpuMemSuspend` | communicator 已分配、**可通过 suspend 释放**的 GPU 内存（字节） |
| `ncclStatGpuMemSuspended` | communicator 已分配的 GPU 内存当前是否处于挂起状态（`0` = 活跃，`1` = 已挂起） |
| `ncclStatGpuMemPersist` | communicator 已分配、**不能被挂起**的 GPU 内存（字节） |
| `ncclStatGpuMemTotal` | NCCL 跟踪的 communicator 已分配 GPU 内存总量（字节） |

## ncclWindow_t

用于 window 注册和解注册的 NCCL window 对象。
