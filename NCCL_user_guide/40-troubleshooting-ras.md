# 40 · Troubleshooting · RAS

> 原文：Troubleshooting → RAS

**NCCL 2.24 起**提供 RAS（reliability, availability, and serviceability）子系统，用于在运行期间查询 NCCL 作业的健康状况，帮助诊断和调试 **crash 与 hang**。

定位：RAS 是低开销基础设施，应用运行时即可使用。它提供运行中应用的**全局视图**，有助于发现异常个体（例如无响应的进程）。拿到这些信息后，用户再用交互式调试、系统日志分析等手段进一步收敛到根因。

## 工作原理

- RAS 内建于 NCCL，在 **NCCL 初始化期间启动**。
- 由一组线程组成（**每进程一个**），它们相互建立连接形成一个网络，用于交换信息和互相监控健康状况。
- **RAS 网络流量走的是 bootstrap / out-of-band 网络接口上的普通 TCP/IP socket**（即 NCCL 初始化时用的那个接口），因此在典型配置下**不会与主 NCCL 流量（走 RDMA 网络）竞争**。
- RAS 轻量，不应干扰主 NCCL 作业，因此**默认启用**（可用 `NCCL_RAS_ENABLE` 控制）。

RAS 线程之间会通信作业配置的任何变化，并定期交换 **keep-alive** 消息。**某个 NCCL 进程崩溃或 hang 时，其他进程上的 RAS 线程会通过「到该进程的 RAS 连接被关闭或变得无响应」而得知。**

## RAS 查询

RAS 线程在 **`localhost` 的 28028 端口**监听客户端连接（默认值可用 `NCCL_RAS_ADDR` 修改）。

用 `ncclras` 二进制客户端连上这个 socket 查询当前作业状态，结果打印到标准输出。客户端参数：

| 参数 | 作用 |
| --- | --- |
| `-h` | 指定主机名 |
| `-p` | 指定端口 |
| `-v` | 出问题时产生更详细的输出 |
| `-t` | 指定不同的超时（**默认 5 秒**，`0` 表示禁用超时） |
| `-f` | 输出格式：`text`（默认）或 `json`（2.28.7 起） |
| `-m` | 切换到监控模式（2.29 起） |

**客户端通信协议是纯文本的**，所以也可以用 `telnet` 或 `netcat` 代替 `ncclras`。相关命令：

| 命令 | 等价的客户端参数 |
| --- | --- |
| `STATUS` | 默认查询 |
| `VERBOSE STATUS` | `-v` |
| `TIMEOUT <seconds>` | `-t` |
| `SET FORMAT <format>` | `-f` |
| `MONITOR` | `-m` |

例如：

```bash
echo verbose status | nc localhost 28028
```

无论以哪种方式提交查询，接收查询的 RAS 线程会回送**作业摘要信息**和**所有 NCCL communicator 的摘要信息**。后者是从作业的所有进程收集来的，**因此对于出问题的作业或特别大的作业，响应可能需要几秒钟才能生成**。遇到问题时会提供额外信息。

## 输出解读

> 具体格式和信息范围随版本变化，以下是理解要点。

### 正常作业

输出包含两部分：

- **Job summary**：节点数、每节点进程数、每进程 GPU 数、总进程数、总 GPU 数。
- **Communicators**：按组（Group）汇总，每行给出组内 comm 数、每 comm 节点数、每节点 rank 数、每 comm rank 数、组内 rank 总数、**Status** 和 **Errors** 两列。

**RAS 会把大小和其他重要属性相同的对象归组**，以尽量缩短摘要输出。

正常状态下 Status 为 `RUNNING`、Errors 为 `OK`。

### MISMATCH：不同 rank 的信息不一致

Errors 列出现 `MISMATCH` 表示**不同 communicator rank 提供的信息之间存在不一致**，详情打印在下方（出现在 **Warnings** 段说明严重性较低）。

典型情形是各 rank 的**集合操作计数不同**，例如"26 个 rank 已发起到操作 6650，6 个 rank 已发起到操作 6649"，并列出后者的 rank、GPU、进程和节点。

**communicator 的标识方式**：`#<x>-<y>`，`x` 是摘要输出中的组号，`y` 是组内的 communicator 编号，**都从 0 开始**。标识符后面跟一个 **communicator hash**——这个值在 NCCL 常规 debug 输出中也能找到，便于交叉对照。

**RAS 会把相关属性相同的 rank 归到一组**（这里是已发起的集合操作计数；**2.26 起会按集合操作类型分别统计**）。**若某个组构成异常个体（outlier），RAS 会打印其每个成员的详细信息。** 默认条件是：组的大小不超过总数的 25% 且成员不超过 10 个；**开启 verbose 输出会放宽到不足总数的 50%，并取消组大小限制。**

**怎么判断要不要担心**：

- **只要重复查询时计数在增长，这种情况就不必担心。** NCCL 集合操作为速度优化，很容易跑得比 RAS 的集合查询快，集合规模较小时尤其如此。
- 应用也可能本身存在负载不均，某些 rank 例行地比其他 rank 更晚到达集合操作——**需要对特定负载的经验来判断什么是正常的**。
- **但如果后续 RAS 查询的输出没有变化，可能说明 communicator 因某种原因"卡住"了，值得调查。**

### 初始化/拆除期间的过渡态

communicator 初始化或拆除期间也会看到类似效果。摘要中可能同时出现多个组，Status 分别为 `FINALIZE`、`RUNNING`、`INIT`。

详情中会看到"ranks 有不同的 status"，例如部分 rank 是 `UNKNOWN`、部分 `RUNNING`、部分 `FINALIZE`。

**关于 `UNKNOWN` 的含义**：这些 rank 在 RAS 收集数据时没有提供关于该 communicator 的任何信息，**原因很简单——它们对 `ncclCommFinalize` 的调用已经完成，因此实际上已不再是该 communicator 的成员**。（**NCCL 2.26 及以后会打印 `NOCOMM` 而不是 `UNKNOWN`**。）

同样，**只要重复查询时情况已解决，就可以忽略。**

### INCOMPLETE：缺少某个进程的数据

Errors 列出现 `INCOMPLETE`，并在 **Errors** 段说明缺少哪个进程/哪个 rank 的 communicator 数据，同时 Warnings 段可能出现 `TIMEOUT`（收集数据时遇到通信超时）。

**这种情况下摘要生成会花几秒**，因为 RAS 在等那个出问题的进程的数据（进程无响应——比如被 stop 了——但 RAS 还不知道）。**重复查询会快得多**，因为一旦 RAS 判定某进程无响应，它会重新配置 RAS 网络绕过它。

### DEAD：进程被判定死亡

**RAS 会尝试重新与无响应的进程建立通信；若 60 秒内无法做到，就将该进程永久判定为 dead**，Errors 段出现 `DEAD` 并给出进程、节点和 GPU 信息。

之后 **RAS 不再尝试通过 RAS 网络与这类进程通信，是否需要进一步动作由用户判断。**

## JSON 输出

**NCCL 2.28.7 起**，RAS 可以生成 JSON 格式输出，以支持机器可解析的指标采集。用 `-f json` 或 `SET FORMAT json`。

**JSON 输出比 text 输出啰嗦得多**（text 是为人阅读优化的）。它本质上是 RAS 采集到的**全部原始数据的转储，分析和解释交给消费者**。

顶层字段包括 `nccl_version`、`cuda_runtime_version`、`cuda_driver_version`、`timestamp`、`communicators_count`、`communicators` 数组，以及 `ras`（含 `collection_time_sec`、`timeouts_count`）。每个 communicator 含 `hash`、`secondary_hash`、`size`、`ranks_count`、`missing_ranks_count`、`ranks` 和 `missing_ranks` 数组。

多数字段自解释，以下几个需要说明：

| 字段 | 说明 |
| --- | --- |
| `secondary_hash` | 与 `hash` 一起使用可以构造**保证唯一**的 communicator 标识符（`hash` 本身不保证唯一，尽管实际碰撞概率极低） |
| `nvml_dev` | 通常与 `cuda_dev` 相同，**除非使用了 `CUDA_VISIBLE_DEVICES`**——那种情况下 `cuda_dev` 在单节点各 rank 间可能不唯一，而 **`nvml_dev` 始终唯一** |
| `init_state` | `0`（`ncclSuccess`）表示 rank 已完全初始化，`7`（`ncclInProgress`）表示初始化仍在进行；**其他任何值都表示错误** |
| `async_error` | 该 rank 调 `ncclGetAsyncError` 会返回的值 |
| `unresponsive` | `true` 表示 RAS 收集数据时无法触达该 rank——**这是 rank 被视为 missing 的最常见原因**。但也存在 communicator 初始化和终止期间的边角情况：进程可达，但它不认为自己是该 communicator 的成员 |

每个 rank 的 `status` 对象含 `init_state`、`async_error`、`finalize_called`、`destroy_flag`、`abort_flag`；`collective_counts` 按操作类型（Broadcast / Reduce / AllGather / ReduceScatter / AllReduce）分别计数。`missing_ranks` 中的 rank 其 `status` 是 `unresponsive` 和 `considered_dead` 两个布尔值。

## 监控模式

**NCCL 2.29 起**，RAS 增加监控模式用于实时状态更新。用 `-m` 或 `MONITOR` 命令。

进入监控模式后，RAS 客户端打印欢迎消息，**在被中断或作业结束之前不会退出**：

```
RAS Monitor Mode - watching for peer changes (Ctrl+C to exit)...
================================================================
```

关注的事件形如进程被判定死亡：

```
[2025-12-19 13:07:07] PEER_DEAD: Process 1524345 on node 172.16.64.245 managing GPU 1
```

监控模式也可以配合 JSON 输出使用，事件对象含 `timestamp`、`group`（如 `LIFECYCLE`）、`event`（如 `PEER_DEAD`）、`peer` 和 `details`。

**一个格式差异要注意**：与前面 communicator 输出中"每个 rank 单独打印"不同，这里关注的实体是**进程**，所以 `cuda_devs` 和 `nvml_devs` 是**数组**（因为一个进程可以管理多个 GPU）。

## 使用速查

| 目的 | 命令 |
| --- | --- |
| 快速查状态 | `ncclras` 或 `echo status \| nc localhost 28028` |
| 问题详情 | `ncclras -v` |
| 大作业/卡住的作业（放宽超时） | `ncclras -t 0` |
| 机器采集 | `ncclras -f json` |
| 实时盯事件 | `ncclras -m` |
| 判断是否真卡住 | **重复查询，看计数/状态是否变化** |

相关环境变量：`NCCL_RAS_ENABLE`、`NCCL_RAS_ADDR`
