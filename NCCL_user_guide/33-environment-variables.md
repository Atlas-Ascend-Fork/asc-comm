# 33 · Environment Variables

> 原文：Environment Variables

NCCL 有一套庞大的环境变量用于针对特定用法调优。

## 设置方式与两类变量

环境变量也可以静态地写在配置文件里：`/etc/nccl.conf`（管理员设置系统级值）或用户 home 下的 `.nccl.conf`（见 `NCCL_CONF_FILE`）。文件内容形如：

```
NCCL_DEBUG=WARN
NCCL_SOCKET_IFNAME==ens1f0
```

**两类变量要区别对待**：

| 类别 | 用法 |
| --- | --- |
| **System configuration** | 让 NCCL 遵循系统特定配置所必需，**可以保留在脚本和系统配置中** |
| **Debugging** | **不应用于生产，也不应保留在脚本中**。只作为临时 workaround 使用，问题解决后立刻移除。**一直设着可能导致次优行为、崩溃或 hang** |

Debugging 类变量的通用警告：新版 NCCL 的行为可能不同，强制它们为某个特定值会**阻止 NCCL 自动选择最佳设置**，因此长期看可能造成性能问题，甚至破坏某些功能。做实验或调试问题时可以用，但一般不要为生产代码设置。

---

# 第一部分：System configuration

## Socket 与网络接口

### NCCL_SOCKET_IFNAME

指定用于通信的 IP 接口。

取值是**前缀列表**，用于过滤 NCCL 使用的接口：

- 多个前缀用 `,` 分隔
- 用 `^` 开头表示**排除**以列表中任意前缀开头的接口
- 用 `=` 开头表示**精确匹配**接口名

| 例子 | 含义 |
| --- | --- |
| `eth` | 使用所有以 `eth` 开头的接口（`eth0`、`eth1`…） |
| `=eth0` | 只用 `eth0` |
| `=eth0,eth1` | 只用 `eth0` 和 `eth1` |
| `^docker` | 不用任何以 `docker` 开头的接口 |
| `^=docker0` | 不用 `docker0` |

**默认行为**：loopback（`lo`）和 docker 接口（`docker*`）**不会被选中，除非没有其他可用接口**。想优先用它们必须显式指定。默认算法还会**偏好以 `ib` 开头的接口**。

**注意：设置该变量会绕过自动接口选择算法**，可能使用所有匹配手动选择的接口。

### 其他 socket 相关变量

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_SOCKET_FAMILY` | — | 强制只用 IPv4 或 IPv6。设 `AF_INET` 强制 IPv4，`AF_INET6` 强制 IPv6 |
| `NCCL_SOCKET_MAGIC` | — | 覆盖 NCCL 内部 TCP socket 握手用的 64 位 magic 值（用于 Socket 网络插件和 IB 连接 bootstrap socket）。**不影响**用 `NCCL_COMM_ID` 或 `ncclGetUniqueId()` 的 bootstrap communicator，那些用各自 per-communicator 的 magic。**在作业内每个进程设成相同值**（例如 Slurm 下用 `0x$(printf '%x' "$SLURM_JOB_ID")`）可以减少无关负载共享节点和端口时的意外串扰。取值：未设或空则用内置默认；否则是十进制或十六进制非负整数（可带 `0x`），按 `strtoull(..., 0)` 解析；非法字符串会回退到内置默认并告警 |
| `NCCL_SOCKET_RETRY_CNT` | 2.24 | 遇到 `ETIMEDOUT`、`ECONNREFUSED` 或 `EHOSTUNREACH` 后重试建立 socket 连接的次数。**默认 34**，任意正值有效 |
| `NCCL_SOCKET_RETRY_SLEEP_MSEC` | 2.24 | 首次上述错误后重试前等待的毫秒数。**后续错误的等待时间随错误次数线性增长**，总时间为 `(N+1) * N/2 * 本值`，N 由 `NCCL_SOCKET_RETRY_CNT` 给出。两者默认值下总重试时间约 60 秒。**默认 100 毫秒** |
| `NCCL_SOCKET_POLL_TIMEOUT_MSEC` | 2.28 | poll 的超时（毫秒），可**降低 bootstrap 期间的 CPU 占用**。取非负整数，**默认 0**（旧行为：不 poll，不间断地尝试推进 socket 操作）；非零则先 poll 至多该时长再尝试推进 |
| `NCCL_SOCKET_NTHREADS` | 2.4.8 | socket transport 每个网络连接使用的 CPU 辅助线程数。增大可能提升 socket 传输性能，**代价是 CPU 占用更高**。取值 1–16。**AWS 默认 2；Google Cloud gVNIC 接口实例默认 4（2.5.6 起）；其他情况默认 1**。通用 100G 网络可手动设为 4。**但本值与 `NCCL_NSOCKS_PERTHREAD` 的乘积不能超过 64** |
| `NCCL_NSOCKS_PERTHREAD` | 2.4.8 | socket transport 每个辅助线程打开的 socket 数。**单 socket 速度受限的环境下**设大于 1 可能改善网络性能。**AWS 默认 8；其他情况默认 1**。通用 100G 网络可手动设为 4。同样受乘积不超过 64 的限制 |

## NCCL_CROSS_NIC

控制是否允许 ring/tree 使用不同的 NIC，从而使节点间通信在不同节点上使用不同 NIC。

背景：使用多 NIC 时为最大化节点间通信性能，NCCL 会**尽量在节点间通信时用相同的 NIC**，以适配「每个 NIC 连到不同网络交换机（网络 rail）」的网络设计，避免流量互相干扰。因此本设置**取决于网络拓扑，尤其是网络 fabric 是否 rail-optimized**。**只有一个 NIC 的系统上无影响。**

| 值 | 含义 |
| --- | --- |
| `0` | 同一 ring/tree 总是用相同 NIC，避免跨网络 rail。适合有 per-NIC 交换机（rail）且 rail 间连接较慢的网络。**注意：若 communicator 在各节点上不是同一组 GPU，NCCL 仍可能需要跨 NIC 通信** |
| `1` | 允许同一 ring/tree 使用不同 NIC。适合节点内所有 NIC 是共置的网络 |
| `2`（默认） | 见 `38-troubleshooting-performance.md` 的说明 |

## InfiniBand / RoCE

### NCCL_IB_HCA

指定用于通信的 Host Channel Adapter（RDMA）接口。

取值格式：逗号分隔的列表，每项形如 **`<hca>[:<port>[:<rail>[:<plane>]]]`**，`<hca>` 之后的字段可选、用 `:` 分隔。

- 前缀 `^` 表示这是**排除列表**
- 第二个前缀 `=` 表示 token 是**精确名称**，否则默认每个 token 按前缀处理
- **省略 `<port>` 时使用该 HCA 的所有端口**
- **若指定了 `<rail>` 或 `<plane>` 但省略了 `<port>`，`<port>` 字段仍必须以空字段占位**以保持字段位置。例如 `mlx5_0::0:0` 表示 `mlx5_0` 的所有端口、rail 0、plane 0
- `<rail>` 和 `<plane>` 为关联设备赋予 rail 和 plane 身份，**省略时都默认为 -1（未分配）**

例子：`mlx5` 用所有以 mlx5 开头的卡的所有端口；`=mlx5_0:1,mlx5_1:1` 用 `mlx5_0` 和 `mlx5_1` 的端口 1。

### NCCL_IB_TIMEOUT 与 NCCL_IB_RETRY_CNT

**这两个要一起看**：等待响应的总时间是两者的**乘积**。

`NCCL_IB_TIMEOUT` 控制 IB Verbs 超时，超时计算为 **4.096 µs × 2^timeout**，最佳值取决于网络规模。**在超大网络上增大该值有帮助**，例如 NCCL 在 `ibv_poll_cq` 调用上以 error 12 失败时。参见 IB 规范 Vol.1 第 12.7.34 节（Local Ack Timeout）。

取值：**默认 20（2.23 起；2.14 起曾是 18，更早是 14）**，可取 0–31。**注意：设为 0 或 ≥ 32 会得到无限超时。**

对照关系（部分）：

| timeout 值 | 单次超时 | 配 retry=7 的总时间 |
| --- | --- | --- |
| 17 | 536.9 ms | 3.8 秒 |
| 18 | 1.1 秒 | 7.5 秒 |
| 19 | 2.1 秒 | 15 秒 |
| **20（默认）** | **4.3 秒** | **30 秒** |
| 21 | 8.6 秒 | 60 秒 |
| 22 | 17.2 秒 | 2 分钟 |
| 23 | 34.4 秒 | 4 分钟 |
| 24 | 1.1 分钟 | 8 分钟 |
| 25 | 2.3 分钟 | 16 分钟 |

`NCCL_IB_RETRY_CNT`（2.1.15 起）控制 IB 重试次数，**默认 7，有效值 0–7**。参见 IB 规范 Vol.1 第 12.7.38 节。

### GID 与地址相关

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_IB_GID_INDEX` | 2.1.4 | RoCE 模式下使用的 Global ID index。用 IB 的 `show_gids` 命令来确定该值。**默认 -1**。（**2.21 及以后 GID index 动态选择，不应设置本变量**，见 `36`） |
| `NCCL_IB_ADDR_FAMILY` | 2.21 | 当 `NCCL_IB_GID_INDEX` 未设时，NCCL 动态选择的 IB GID 所关联的 IP 地址族。**默认 `AF_INET`** |
| `NCCL_IB_ADDR_RANGE` | 2.21 | 当 `NCCL_IB_GID_INDEX` 未设时，NCCL 动态选择的有效 GID 范围。**默认未设即忽略**。可用 CIDR 格式定义 IPv4/IPv6 的 GID 范围 |
| `NCCL_IB_ROCE_VERSION_NUM` | 2.21 | 当 `NCCL_IB_GID_INDEX` 未设时，动态选择的 IB GID 所关联的 RoCE 版本。**默认 2** |

### QoS 与流量类

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_IB_SL` | 2.1.4 | IB Service Level。**默认 0** |
| `NCCL_IB_TC` | 2.1.15 | IB traffic class 字段。**默认 0** |
| `NCCL_IB_FIFO_TC` | 2.22.3 | **控制消息**的 IB traffic class。控制消息是控制 credit 返回的短 RDMA write 操作，与传输大数据段的其他 RDMA 操作不同。让它们使用高优先级、低延迟的 traffic class，**避免被其余流量拖慢**。**默认为 `NCCL_IB_TC` 设定的值（未设则为 0）** |

### 其他 IB 变量

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_IB_RETURN_ASYNC_EVENTS` | 2.23 | IB 事件以警告形式报告给用户。启用后 NCCL **还会在致命的 IB 异步事件发生时停止 IB 通信**。**默认 1**，设 0 禁用 |

## Out-of-band 网络

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_OOB_NET_ENABLE` | 2.23 | 启用 NCCL net 用于 out-of-band 通信。启用后会**改变 communicator 初始化期间 allgather 的实现**。设 0 禁用、1 启用 |
| `NCCL_OOB_NET_IFNAME` | 2.23 | 启用 OOB net 时指定用哪些网络接口。可接受的接口列表取决于 NCCL 使用的网络。逗号分隔；端口号用 `:` 指定；前缀 `^` 表示排除列表，第二个前缀 `=` 表示精确名称（否则按前缀处理）。**指定多个设备时，NCCL 选列表中第一个匹配的设备**。例：`NCCL_NET="IB" NCCL_OOB_NET_ENABLE=1 NCCL_OOB_NET_IFNAME="=mlx5_1"` 用 IB NET 的 `mlx5_1` 接口；去掉 `=` 则会在 `mlx5_1`、`mlx5_10`、`mlx5_11` 等中取第一个找到的 |

## UniqueId 交错（staggering）

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_UID_STAGGER_THRESHOLD` | 2.23 | 触发「NCCL rank 与 `ncclUniqueId` 之间通信交错」的阈值，**目的是避免压垮 `ncclUniqueId`**。若通信的 rank 数超过该阈值，就按 rank 值做交错；**若每个 `ncclUniqueId` 的 rank 数小于或等于阈值则不交错**。例：128 rank + 1 个 id + 阈值 64 → 交错；128 rank + 2 个 id + 阈值 64 → 不交错。必须是严格正整数，**默认 256** |
| `NCCL_UID_STAGGER_RATE` | 2.23 | 交错时目标消息速率，用于计算某个 rank 需要等待多久。严格正整数，单位**消息/秒**，**默认 7000** |

## 网络与插件选择

### NCCL_NET

（2.10 起）强制 NCCL 使用特定网络，例如确保用某个外部插件而不自动回退到内部 IB 或 Socket 实现。

**设置本变量会覆盖所有 communicator 中的 `netName` 配置**（见 `ncclConfig_t`）；未设则由配置决定；不传配置则 NCCL 自动选择最佳网络模块。

取值必须与所用 NCCL 网络的名称**精确匹配（大小写不敏感）**。内部网络名为 `"IB"`（通用 IB verbs）和 `"Socket"`（TCP/IP sockets），外部插件自定义名称。**默认未定义。**

### 插件加载变量

四个插件变量遵循相似的查找策略：**设了就先按给定名字加载，失败则加载 `libnccl-<类型>-<值>.so`；未设则加载默认 `libnccl-<类型>.so`；都找不到就用内部实现。** 取值都可以是插件后缀、插件文件名，或 `"none"`。

| 变量 | 起始版本 | 查找的默认库 | 特别之处 |
| --- | --- | --- | --- |
| `NCCL_NET_PLUGIN` | 2.11 | `libnccl-net.so` | 失败时尝试 `libnccl-net-<值>.so` |
| `NCCL_TUNER_PLUGIN` | — | `libnccl-tuner.so` | **若没找到，会在 net 插件中查找 tuner 符号**（见 `NCCL_NET_PLUGIN`）；都没有才用内部 tuner |
| `NCCL_PROFILER_PLUGIN` | — | `libnccl-profiler.so` | **没找到则不启用 profiling**。设为 `STATIC_PLUGIN` 时在程序二进制中查找插件符号 |
| `NCCL_ENV_PLUGIN` | 2.28 | `libnccl-env.so` | 可设为库名（如 `/path/to/library/libfoo.so`）或后缀（如 `foo` → `libnccl-env-foo.so`）；设 `"none"` 显式禁用外部插件用内部的 |

## 其他系统配置

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_IGNORE_CPU_AFFINITY` | 2.4.6 | 让 NCCL **忽略作业提供的 CPU 亲和性，只用 GPU 亲和性**。默认 0，设 1 启用 |
| `NCCL_CONF_FILE` | 2.23 | 指定静态配置文件路径。**路径中不接受 `~` 字符**，请先转成相对或绝对路径。未设（或版本早于 2.23）时，NCCL 使用 home 目录下的 `.nccl.conf`（若存在） |
| `NCCL_SET_THREAD_NAME` | 2.12 | 给 NCCL CPU 线程更有意义的名字以便调试和分析。0 或 1，**默认 0（禁用）** |

## 日志相关

（详细用法见 `39-troubleshooting-logging.md`）

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_DEBUG` | — | `VERSION`（启动时打印版本）、`WARN`（任何 NCCL 调用出错时打印明确错误消息）、`INFO`（打印调试信息）、`TRACE`（每次调用打印可重放的 trace 信息） |
| `NCCL_DEBUG_FILE` | 2.2.12 | 把调试日志输出到文件，文件名可用 `filename.%h.%p` 形式（`%h` 主机名、`%p` PID）。**不接受 `~`**。未设时默认输出到 stdout。也可设为 `/dev/stdout` 或 `/dev/stderr`，**这会使输出变成行缓冲**。**设置本变量会创建并覆盖同名的已有文件**。注意：文件名若在作业各进程间不唯一，输出可能丢失或损坏 |
| `NCCL_DEBUG_SUBSYS` | 2.3.4 | 按子系统过滤 `NCCL_DEBUG=INFO` 输出，逗号分隔；名字前加 `^` 表示禁用该子系统的日志。**默认 `INIT,BOOTSTRAP,ENV`**。支持的名字见 `39`（注意本页还列出了 `ALLOC_HOST` 用于 host 内存分配，与 `ALLOC` 的非 host 内存分配相区分） |
| `NCCL_DEBUG_TIMESTAMP_FORMAT` | 2.26 | 时间戳格式，值传给 `strftime`。**默认 `[%F %T]`** 即 `[YYYY-MM-DD HH:MM:SS]`；设为空则不打印时间戳。时间按**本地时间**打印，可通过 `TZ` 环境变量改变（`TZ=UTC` 用 UTC）。除 strftime 支持的转换外，还可用 `%Xf`（X 为 1–9 的单个数字）打印秒的小数部分。**格式中的下划线渲染为空格**。注意非 call 类的 TRACE 日志仍打印「自 NCCL 调试子系统初始化以来的微秒数」 |
| `NCCL_DEBUG_TIMESTAMP_LEVELS` | 2.26 | 哪些级别的日志带时间戳，逗号分隔。有效级别：`VERSION`、`WARN`、`INFO`、`ABORT`、`TRACE`；`ALL` 表示全部；空值表示全部禁用；**以 `^` 开头表示列出的级别不带、其余带**。**默认只有 `WARN` 带时间戳** |

## CollNet 与 CTA 策略

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_COLLNET_ENABLE` | 2.6 | 启用 CollNet 插件。**默认 0**，设 1 启用 |
| `NCCL_COLLNET_NODE_THRESHOLD` | 2.9.9 | 节点数低于该阈值时不启用 CollNet。**默认 2** |
| `NCCL_CTA_POLICY` | 2.29（legacy 值自 2.27） | 设置 communicator 的 CTA 策略。取 `DEFAULT`（或 legacy `0`，默认）、`EFFICIENCY`（或 `1`）、`ZERO`（或 `2`）。**非 legacy 策略可以用 `\|` 组合多个**。策略含义见 `22-api-flags.md` |
| `NCCL_NETDEVS_POLICY` | 2.28 | 网络设备到 GPU 的分配策略。NCCL 会自动检测每个 GPU 可用的网络设备，考虑网络带宽和节点拓扑。`AUTO`（默认）还会考虑同一 communicator 内的其他 GPU；**特定场景下该策略可能导致不同 communicator 的不同 GPU 共享同一网络设备，从而影响性能**。`MAX:N` 表示每个 GPU 最多用 N 个可用网络设备（用于 AUTO 下发生设备共享并影响性能时）。`ALL` 表示每个 GPU 使用所有可用网络设备，不考虑其他 GPU |

## 多 rank 共享 GPU（实验特性）

### NCCL_MULTI_RANK_GPU_ENABLE

（2.30 起，**实验性**）允许 NCCL 在多个 rank 使用同一 GPU 设备（或 MloPart 启用时的同一分区）的情况下运行。

关键约束：

- **设置本变量本身不会把多个 rank 分配到一个 GPU**，应用仍必须自己安排 rank 共享设备。
- **每个共享 GPU 的 rank 都分配自己的一套 channel 和相关资源。** 确保设备上有足够资源（SM、内存等）容纳所有 rank 是**用户的责任**，**做不到会导致 NCCL hang**。
- 默认配置下 NCCL 应能支持**每 GPU 最多 2 个 rank**。
- **建议用 `NCCL_MAX_CTAS` 限制每个 rank 的 channel 数以防资源耗尽。**
- **NVLS 当前与「同一 communicator 内多个 rank 使用同一 GPU」不兼容**：`NCCL_NVLS_ENABLE=1` 时检测到每 GPU 多 rank 会导致 communicator 初始化失败；`NCCL_NVLS_ENABLE=2`（默认）时 NVLS 被静默禁用。

## 拓扑文件

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_TOPO_FILE` | 2.6 | 在检测拓扑之前加载的 XML 文件路径。默认 NCCL 会加载 `/var/run/nvidia-topologyd/virtualTopology.xml`（若存在）。**注意：对多节点 NVLink 系统，尽管 `NCCL_TOPO_DUMP_FILE` 产生的文件含完整 NVLink 域拓扑，`NCCL_TOPO_FILE` 应只包含单节点拓扑** |
| `NCCL_TOPO_DUMP_FILE` | 2.6 | 检测后把 XML 拓扑 dump 到该文件路径（会创建或覆盖）。**多节点 NVLink 系统上 dump 出的文件含完整 NVLink 域拓扑** |

---

# 第二部分：Debugging

> **再次提醒**：本节变量要谨慎使用。强制特定值会阻止 NCCL 自动选择最佳设置，长期看可能导致性能问题甚至破坏功能。适合实验和调试，**不应用于生产**。

## Transport 开关

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_P2P_DISABLE` | — | 禁用 P2P transport（GPU 间经 NVLink 或 PCI 的 CUDA 直接访问）。设 1 禁用 |
| `NCCL_P2P_DIRECT_DISABLE` | — | 禁止 NCCL 通过 P2P **直接访问同一进程内 GPU 之间的用户 buffer**。适用场景：用户 buffer 是用「不会自动让同进程其他 GPU 可访问」的 API 分配的。设 1 禁用 |
| `NCCL_SHM_DISABLE` | — | 禁用共享内存（SHM）transport。SHM 在无法 P2P 时用于设备之间（走 host 内存）。**禁用后 NCCL 会用网络（IB 或 IP socket）在 CPU socket 之间通信**。设 1 禁用 |
| `NCCL_IB_DISABLE` | — | 阻止使用 IB/RoCE transport，**回退到 IP socket**。设 1 禁用 |
| `NCCL_NVB_DISABLE` | 2.11 | 禁用「经中间 GPU 通过 NVLink 的节点内通信」。默认 0，设 1 禁用 |
| `NCCL_PXN_DISABLE` | 2.12 | 禁用「使用非本地 NIC、经 NVLink 和中间 GPU 的节点间通信」。默认 0，设 1 禁用 |

### NCCL_P2P_LEVEL

（2.3.4 起）精细控制何时在 GPU 之间使用 P2P transport。**level 定义 NCCL 使用 P2P 的 GPU 之间最大距离。** 不指定时 NCCL 会按架构和环境尝试最优选择。

| 值 | 含义 |
| --- | --- |
| `LOC` | 从不使用 P2P（始终禁用） |
| `NVL` | GPU 通过 NVLink 连接时使用 P2P |
| `PIX` | GPU 在同一 PCI switch 上时使用 |
| `PXB` | GPU 通过 PCI switch 连接（可能多跳）时使用 |
| `PHB` | GPU 在同一 NUMA 节点时使用，**流量经过 CPU** |
| `SYS` | 在 NUMA 节点之间使用，可能跨 SMP 互连（如 QPI/UPI） |

也可以声明为对应路径类型的整数（**legacy**，为向后兼容保留）。

### NCCL_NET_GDR_LEVEL

（2.3.4 起；原名 `NCCL_IB_GDR_LEVEL`，2.4.0 改名）精细控制何时在 NIC 与 GPU 之间使用 GPU Direct RDMA。**level 定义 NIC 与 GPU 之间的最大距离。** 不指定时 NCCL 按架构和环境尝试最优选择。

取值同样是路径类型字符串：`LOC`（从不使用）、`PIX`（GPU 与 NIC 在同一 PCI switch 时）、`PXB`（通过 PCI switch 连接、可能多跳时）、`PHB`（同 NUMA 节点时）等。

## 缓冲区与线程/通道数

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_BUFFSIZE` | — | NCCL 在 GPU 对之间通信时使用的 buffer 大小。**遇到内存约束问题，或认为换个 buffer 大小能改善性能时使用**。**默认 4194304（4 MiB）**，单位字节，**建议用 2 的幂** |
| `NCCL_NTHREADS` | — | 每个 CUDA block 的 CUDA 线程数。**NCCL 每个通信 channel 启动一个 CUDA block。** 认为 GPU 时钟偏低想增加线程数时用，也可以用它减少线程数以降低 GPU 负载。**较新 GPU 默认 512，某些较老代默认 256。允许值：64、128、256、512** |
| `NCCL_MAX_NCHANNELS` | `NCCL_MAX_NRINGS` 自 2.0.5，本名自 2.5.0，**2.17 废弃** | 限制 NCCL 可用的 channel 数。**减少 channel 数也减少用于通信的 CUDA block 数**，从而减小对 GPU 计算资源的影响。旧的 `NCCL_MAX_NRINGS` 在新版本中仍作为别名有效，但若设了本变量则被忽略。**已被 `NCCL_MAX_CTAS` 取代**；两者都设时取**更严格（更小）**的值。取 ≥ 1 的任意值 |
| `NCCL_MIN_NCHANNELS` | `NCCL_MIN_NRINGS` 自 2.2.0，本名自 2.5.0，**2.17 废弃** | 控制希望 NCCL 使用的最小 channel 数。**增加 channel 数也增加 CUDA block 数，可能改善性能但占用更多 CUDA 计算资源。** 在「NCCL 通常只会创建一个 channel」的平台上使用聚合集合操作时特别有用。**已被 `NCCL_MIN_CTAS` 取代**；两者都设时取**更严格（更大）**的值。默认随平台而定；上限为 12（到 2.2）、16（2.3–2.4）或 32（2.5 及以后）。**注意小消息时 NCCL 可能使用比该值更少的 channel** |

## 参数检查

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_CHECKS_DISABLE` | 2.0.5 起，**2.2.12 废弃** | 禁用每次集合调用的参数检查。**检查在开发期有用但会增加延迟**，生产环境可禁用以提升性能。默认 0，设 1 禁用 |
| `NCCL_CHECK_POINTERS` | 2.2.12 起，**2.29.7 废弃** | 启用每次集合调用的 CUDA 内存指针检查。**已被 `NCCL_CHECK_MODE=DEBUG_LOCAL` 取代，未来版本可能移除**。默认 0；设 1 恢复 2.2.12 之前的原始行为 |
| `NCCL_CHECK_MODE` | 2.29.7 | 输入参数检查模式。`DEFAULT`（默认）不检查；`DEBUG_LOCAL` 本地检查参数/指针；`DEBUG_GLOBAL` 全局检查（如对称 buffer 检查等） |

## Kernel 启动

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_LAUNCH_MODE` | 2.1.0 起，**2.9 废弃**，未来可能移除 | 控制 NCCL 如何启动 CUDA kernel。**默认 `PARALLEL`**；设为 `GROUP` 会对管理多于一个 GPU 的进程使用 cooperative group（需 CUDA 9.0+） |
| `NCCL_SET_STACK_SIZE` | 2.9 | 把 CUDA kernel 栈大小设为所有 NCCL kernel 中的最大值，**可避免加载时的 CUDA 内存重配置**。**如果遇到因 CUDA 内存重配置导致的 hang 就设为 1**。默认 0 |
| `NCCL_MEM_SYNC_DOMAIN` | 2.16 | 设置 NCCL kernel 的默认 Memory Sync Domain（CUDA 12.0 & sm90 及以后）。**当 NCCL kernel 与应用计算 kernel 使用不同 domain 时，有助于消除相互干扰**。默认 `cudaLaunchMemSyncDomainRemote`（1），当前支持 0 和 1 |
| `NCCL_LAUNCH_ORDER_IMPLICIT` | 2.26 | 用 **host 程序顺序**隐式地为同一设备上不同 communicator 的 NCCL 操作定序，**确保它们不会死锁**。CUDA runtime 和 driver 为 **12.3+ 时允许重叠执行**；更老的 CUDA 版本上操作会被串行化。默认 0，设 1 启用（用法见 `04`） |
| `NCCL_LAUNCH_RACE_FATAL` | 2.26 | 尝试捕获「多个 host 线程竞争向同一设备 launch」的情况，若发现则**返回致命错误**。这种竞争会破坏 `NCCL_LAUNCH_ORDER_IMPLICIT` 所依赖的程序顺序确定性。**默认 1（启用）**，设 0 禁用 |

## IB 高级选项

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_IB_AR_THRESHOLD` | 2.6 | 超过该阈值就把 IB 数据放在单独的消息中发送，**以便利用 adaptive routing**。单位字节，**默认 8192**。**设得比 `NCCL_BUFFSIZE` 更大会完全禁用 adaptive routing** |
| `NCCL_IB_OOO_RQ` | 2.30 | 允许 QP 接收端的 Receive WR **被乱序消费**。默认 0（禁用），设 1 强制启用 |
| `NCCL_IB_QPS_PER_CONNECTION` | 2.10 | 两个 rank 之间每个连接使用的 IB queue pair 数。**在需要多个 QP 才能有良好路由熵的多级 fabric 上有用**。取 1–128，**默认 1**。数据如何在多 QP 上切分见下一条，会影响性能 |
| `NCCL_IB_SPLIT_DATA_ON_QPS` | 2.18 | 创建多个 QP 时如何使用它们。设 **1（split 模式）**每条消息在各 QP 上**均分**，**QP 很多时可能造成明显的延迟劣化**；设 **0（round-robin 模式）**每次发消息按 round-robin 使用 QP，**不发送多条消息的操作不会用到所有 QP**。**默认 0（2.20 起；2.18 和 2.19 默认是 1）** |
| `NCCL_IB_CUDA_SUPPORT` | **2.4.0 已移除**，见 `NCCL_NET_GDR_LEVEL` | 强制或禁用 GPU Direct RDMA。默认 NCCL 在拓扑允许时启用 GDR。设 0 禁用，设 1 在所有情况下强制使用 |
| `NCCL_IB_PCI_RELAXED_ORDERING` | 2.12 | 为 IB Verbs transport 启用 Relaxed Ordering。**Relaxed Ordering 在虚拟化环境中能大幅帮助 IB 网络性能。** 设 2 自动使用（若可用）、1 强制使用（不可用则失败）、0 禁用。**默认 2** |
| `NCCL_IB_ADAPTIVE_ROUTING` | 2.16 | 为 IB Verbs transport 启用支持 adaptive routing 的数据传输，**可改善大规模通信性能**。需相应选择系统定义的启用了 AR 的 SL（见 `NCCL_IB_SL`）。**IB 网络默认启用（1），RoCE 网络默认禁用（0）** |
| `NCCL_IB_ECE_ENABLE` | 2.23 | 在 IB/RoCE Verbs 网络上启用 Enhanced Connection Establishment（ECE）。**ECE 可用于启用拥塞控制、adaptive routing、selective repeat 等高级网络特性**。注意：这些参数**不由 NCCL 解释或控制**，通过 ECE 机制直接传给 HCA。**默认 1（2.19 起启用）**，设 0 禁用。**警告：系统上 ECE 参数配置不当会反过来损害 NCCL 性能**，若在系统级启用了 ECE，管理员应确保配置正确 |
| `NCCL_IB_MERGE_NICS` | 2.20 | 让 NCCL 把**双端口 IB NIC 合并成单个逻辑网络设备**，便于聚合双端口带宽。**默认 1（启用）**，设 0 禁用 |
| `NCCL_NET_MERGE_POLICY` | 2.30.5 | 控制 NCCL 如何把物理 HCA 合并成虚拟 HCA。该策略**叠加在**已有的基于拓扑距离的融合之上（见 `NCCL_NET_MERGE_LEVEL`），不替代距离逻辑，但能**进一步约束**哪些候选可被合并。**专为 rail-optimized 系统设计**——那里所有物理 HCA 与 GPU 距离相同，**基于距离的逻辑可能误把不同 rail 的设备合并进一个虚拟设备**。`ALL`（默认）无视 rail 归属合并给定 merge level 上的所有设备；`RAIL` 只合并**共享同一 rail 归属**的设备，**所有 rail 归属未定义（`railId == -1`）的设备不会被合并** |

## cuMem 与内存

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_CUMEM_ENABLE` | 2.18 | 用 CUDA `cuMem*` 函数在 NCCL 中分配内存。0 或 1。**2.18 默认 0（禁用）；2.19 起若系统支持则自动启用**（本变量仍可覆盖自动探测） |
| `NCCL_CUMEM_HOST_ENABLE` | 2.23 | 用 CUDA `cuMem*` 函数分配 **host** 内存（见 `37` 的共享内存一节）。**2.23 默认 0；2.24 起，若 CUDA driver ≥ 12.6、CUDA runtime ≥ 12.2 且支持 cuMem host allocation，则默认 1** |
| `NCCL_DMABUF_ENABLE` | 2.13 | 用 Linux **dma-buf** 子系统做 GPU Direct RDMA buffer 注册。dma-buf 让支持 GDR 的 NIC 能**无需 CPU 参与**直接读写 CUDA buffer。**默认 1（启用），但内核或 CUDA/NIC 驱动不支持时会自动禁用** |
| `NCCL_IPC_USE_ABSTRACT_SOCKET` | 2.29 | 创建用于节点内 CUDA IPC handle 交换的 Unix Domain Socket 时使用 Linux Abstract Socket 机制。**默认 1（启用）**，但启用时在某些情况下（例如多容器处于不同 network namespace）**会阻碍节点内 GPU 通信**，此时设 0 |

## GDR 细节

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_NET_GDR_C2C` | 2.26 | 当经「挂在 CPU 上的 NIC」（即距离 PHB）发送数据、且该 CPU 与 GPU 通过 **C2C 互连**相连时启用 GDR。**这实际上会覆盖该 NIC 的 `NCCL_NET_GDR_LEVEL` 设置**。**2.26 默认 0；2.27 起默认 1** |
| `NCCL_NET_GDR_READ` | — | 只要 GPU-NIC 距离在 `NCCL_NET_GDR_LEVEL` 指定范围内，**发送**数据时也启用 GDR。**2.4.2 之前默认禁用**（发送时数据先存到 CPU 内存再进 IB 卡）；**2.4.2 起在基于 NVLink 的平台上默认启用**。**注意：在某些平台（如 PCI-E）上，发送时直接从 GPU 内存读取已知比从 CPU 内存读取略慢**。默认值：2.4.2 前所有平台为 0；2.4.2 起 NVLink 平台为 1，其他为 0 |
| `NCCL_NET_SHARED_BUFFERS` | 2.8 | 允许节点间点对点通信使用共享 buffer。会**为所有远端 peer 使用单个大池，内存占用恒定而非随远端 peer 数线性增长**。**默认 1（启用）**，设 0 禁用 |
| `NCCL_NET_SHARED_COMMS` | 2.12 | 在 PXN 语境下复用相同连接。**允许消息聚合，但也会降低网络包的熵**。**默认 1（启用）**，设 0 禁用 |

## 不一致检测

注意这两个变量的**默认值方向相反**：

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_IGNORE_NET_MISMATCH` | 2.30 | 检测到各 rank 之间 **Net 设备数不一致**时是否让初始化失败。**默认 1（忽略不一致）**；设 0 则在某些 rank 能访问的 Net 设备比其他 rank 少时初始化失败 |
| `NCCL_IGNORE_COLLNET_MISMATCH` | 2.30 | 检测到各 rank 之间 **CollNet 设备数不一致**时是否让初始化失败。**默认 0（不一致就以系统错误失败）**，因为这通常表明误配可能导致故障；设 1 则忽略并继续初始化 |

## 算法与协议选择

### NCCL_ALGO

（2.5 起）定义 NCCL 将使用哪些算法。

可选算法及其可用版本：

| 版本 | 算法 |
| --- | --- |
| 2.5+ | `Ring` |
| 2.5+ | `Tree` |
| 2.5–2.13 | `Collnet` |
| 2.14+ | `CollnetChain` |
| 2.14+ | `CollnetDirect` |
| 2.17+ | `NVLS` |
| 2.18+ | `NVLSTree` |
| 2.23+ | `PAT` |

**`NVLS` 和 `NVLSTree` 启用 NVLink SHARP offload。** 取值为逗号分隔列表（大小写不敏感）；**以 `^` 开头表示排除而非包含**。

**2.24 起**取值形式扩展，更灵活，且**遇到意外 token 会告警并失败**。重要变化：**如果没有把 `ring` 指定为有效算法，当某个函数没有其他有效算法时不会再隐式回退到 ring，而是直接失败。** 新格式是分号分隔的「函数名 + 算法列表」对，第一项的函数名可选（省略则适用于后面未列出的所有函数），函数名与逗号分隔的算法列表之间用冒号分隔。

### NCCL_PROTO

（2.5 起）定义 NCCL 允许使用哪些协议。

**不鼓励用户设置本变量**，例外是怀疑 NCCL 有 bug 时禁用某个特定协议。**特别注意：在不支持 LL128 的平台上启用 LL128 会导致数据损坏。**

取值为逗号分隔列表（大小写不敏感）：`LL`、`LL128`、`Simple`。以 `^` 开头表示排除。**默认启用所有支持的算法**：支持 LL128 的平台上等价于 `LL,LL128,Simple`，否则为 `LL,Simple`。

**2.24 起**取值形式与 `NCCL_ALGO` 一样扩展，允许为每个函数分别指定协议。

### 已移除的阈值变量

| 变量 | 生命周期 | 说明 |
| --- | --- | --- |
| `NCCL_SINGLE_RING_THRESHOLD` | 2.1 起，**2.3 移除** | 低于该值只用一个 ring，**限制带宽但改善延迟**。compute capability 7 及以上默认 262144（256 kB），否则 131072（128 kB） |
| `NCCL_LL_THRESHOLD` | 2.1 起，**2.5 移除** | 低于该值使用低延迟算法。默认 16384（到 2.2）或随 rank 数而定（2.3 及以后） |
| `NCCL_TREE_THRESHOLD` | 2.4 起，**2.5 移除** | 低于该值使用 tree 算法而非 ring。默认随 rank 数而定 |

## PXN

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_P2P_PXN_LEVEL` | 2.12 | 控制 send/receive 操作在哪些情况下使用 PXN。**0** 禁用 send/receive 的 PXN；**1** 在目标偏好的 NIC 无法通过 PCI switch 访问时启用；**2（默认）**总是使用 PXN，即使 NIC 通过 PCI switch 相连，**把节点内所有 GPU 的数据存到一个中间 GPU 上以最大化聚合** |
| `NCCL_PXN_C2C` | 2.27 | 当 peer GPU 通过 **C2C + PCIe** 连到目标 NIC 时允许使用 PXN 机制。**默认 1（2.28 起；2.27 中为 0）** |

## Buffer 注册

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_GRAPH_REGISTER` | 2.11 | NCCL 调用被 CUDA Graph capture 时启用 user buffer 注册。**仅在以下条件同时满足时有效**：(i) 使用 CollNet 算法；(ii) 节点内所有 GPU 相互有 P2P 访问；(iii) 每进程最多一个 GPU。**警告：每进程多 GPU 配 CUDA graph capture 在某些条件下会死锁**（见 `12`）。user buffer 注册可减少用户 buffer 与 NCCL 内部 buffer 之间的数据拷贝次数。**CUDA Graph 销毁时自动解注册。** 默认 1（启用） |
| `NCCL_LOCAL_REGISTER` | 2.19 | 用户显式调 `ncclCommRegister` 时启用本地 buffer 注册。默认 1（启用） |
| `NCCL_LEGACY_CUDA_REGISTER` | 2.24 | 通过 `cudaMalloc`（及相关分配器）分配的是 **legacy buffer**。**注册 legacy buffer 会引起隐式同步，这是不安全的，可能导致 NCCL hang。** NCCL 默认禁用，**用户应转向基于 cuMem 的分配器**。默认 0（禁用） |
| `NCCL_WIN_ENABLE` | 2.27 | 启用 window 内存注册。默认 1（启用） |

## CUDA Graph 相关

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_GRAPH_MIXING_SUPPORT` | 2.13 | 启用/禁用「来自并行 CUDA graph 的多个未完成 NCCL 调用」或「一个 CUDA graph 与非 captured NCCL 调用混合」的支持。**NCCL 调用从 host 侧 launch 起算未完成**（非 captured 是 `ncclAllreduce()` 调用，captured 是 `cudaGraphLaunch()`），到 device kernel 执行完成为止。**禁用支持时以下用例不被支持**：① 从并行 graph launch 中使用同一 communicator（或 split-share 的 communicator），"并行"指在无依赖串行化的不同 stream 上；② 在使用同一 communicator 的 graph launch 未完成期间发起非 captured NCCL 集合操作，**无论 stream 顺序如何**。**之所以提供禁用能力**：观测到启用时若多个 rank 从同一线程通过 `cudaGraphLaunch` 提交工作，CUDA launch 会 hang。默认 1（启用） |
| `NCCL_GRAPH_STREAM_ORDERING` | 2.30 | 允许应用**禁用 NCCL 在 CUDA graph capture 期间对通信 kernel 的内部序列化**，作为 capture 密集型负载的性能优化（例如频繁重新 capture graph、或把每个集合操作包进自己的 CUDA 子图的框架）。**在 CUDA graph capture 之外无效。** 设 **1（默认）**时 NCCL 保证共享 GPU 的各 graph 和 communicator 之间通信 kernel 以**序列化且确定的顺序**执行，应用不承担定序责任；设 **0** 时禁用内部序列化。**警告：`0` 与 graph mixing（communicator `graphUsageMode=2`）的组合不受支持**——若某 communicator 禁用了 stream ordering，graph mixing 必须关闭（用 `graphUsageMode` 0 或 1），**并注意 `NCCL_GRAPH_MIXING_SUPPORT=1` 会在 init 时强制 `graphUsageMode=2`，覆盖显式设定的更低模式**。需要 mixing 的负载必须保持默认 `1`。同样规则适用于 per-communicator 的 `graphStreamOrdering=0` |

## 点对点调优

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_P2P_NET_CHUNKSIZE` | 2.14 | `ncclSend`/`ncclRecv` 操作通过网络发送的消息大小。**默认 131072（128 K）**，单位字节，**建议用 2 的幂**（下一档是 262144） |
| `NCCL_P2P_LL_THRESHOLD` | 2.14 | P2P 操作使用 LL 协议的最大消息大小。十进制数，**默认 16384** |
| `NCCL_ALLOC_P2P_NET_LL_BUFFERS` | 2.14 | 指示 communicator 为**所有 P2P 网络连接**分配专用 LL buffer，使所有 rank 都能对小于 `NCCL_P2P_LL_THRESHOLD` 的延迟受限 send/receive 操作使用 LL 协议。**节点内 P2P 传输总是会分配专用 LL buffer。** **若跑 rank 数很多的 all-to-all 负载，这会带来很高的扩展内存开销。** 默认 0（禁用） |
| `NCCL_RUNTIME_CONNECT` | 2.22 | 在**运行时**（例如调用 `ncclAllreduce()` 时）动态连接 peer，而不是在 init 阶段。默认 1；设 0 则在 init 阶段连接 peer |

## Communicator 行为覆盖

以下变量会**覆盖所有 communicator 中对应的 `ncclConfig_t` 配置**（见 `20-api-types.md`）；未设时由配置决定；不传配置时 NCCL 自动选择。

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_COMM_BLOCKING` | 2.14 | 控制 NCCL 调用是否允许阻塞。**包括所有 NCCL 调用**：init/finalize 函数，以及可能因 send/receive 连接的延迟初始化而阻塞的通信函数。1 表示阻塞、0 表示非阻塞。**默认未定义**（不传配置时 communicator 是阻塞的） |
| `NCCL_CGA_CLUSTER_SIZE` | 2.16 | 设置 CUDA Cooperative Group Array（CGA）cluster 大小。**sm90 及以后多了一层层级，可以把 Grid 内若干 block 编成一组，称为 Thread Block Cluster。** 设为非零会让 NCCL 以相应的 Cluster Dimension 属性启动通信 kernel。取 0–8，**默认未定义** |
| `NCCL_MAX_CTAS` | 2.17 | NCCL 应使用的最大 CTA 数。**增加 CTA 数会消耗更多 GPU 资源但可能提高吞吐。** NCCL 通常优先吞吐，但**在「达到峰值需要大量 GPU 资源」的平台上，最大 CTA 数默认可能被封顶**——这是为了给 GPU 密集型应用更均衡的性能，但显然与负载相关；**特别是为了 benchmark，手动提高上限可能得到更好的吞吐数字**。这是废弃的 `NCCL_MAX_NCHANNELS` 的替代；两者都设时取更严格（更小）的值。取正整数，**上限 64（2.25 之前为 32）** |
| `NCCL_MIN_CTAS` | 2.17 | NCCL 应使用的最小 CTA 数。NCCL 会为每个集合操作估算最优 CTA 数，**若该估算过于保守，可以用本变量提高下限**。**注意：使用比必要更多的 CTA 实际上可能降低性能，小消息时尤其如此**（对非常小的消息，NCCL 可能使用比本值更少的 channel），需谨慎。**benchmark 和测试中常用它强制使用特定 channel 数——此时把最小值和最大值设为相同**。这是废弃的 `NCCL_MIN_NCHANNELS` 的替代；两者都设时取更严格（更大）的值 |

## NVLS 与 MNNVL

### NCCL_NVLS_ENABLE

（2.17 起）启用 NVLink SHARP（NVLS）。NVLS 在**第三代 NVSwitch 系统（NVLink4）配 Hopper 及更新 GPU 架构**上可用，允许把 `ncclAllReduce` 等集合操作 offload 到 NVSwitch 域。

| 值 | 含义 |
| --- | --- |
| `0` | 禁用 NVLS，**不分配任何 NVLS 资源** |
| `1` | 启用 NVLS。**系统不支持时初始化不会失败，但 NVLS 资源分配不成功时会失败** |
| `2`（默认） | 同 `1` |

**一段值得注意的版本历史**：2.27 和 2.28 中，NVLS 资源分配失败时 NCCL 会尝试**静默回退**到其他 transport。但**如果只有部分 rank 分配失败，这会导致 hang**，因此 **2.29 中行为回退到 2.27 之前（即与 `1` 相同）**。未来版本若能可靠实现，可能重新启用静默回退。

### MNNVL 变量

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_MNNVL_ENABLE` | 2.21 | 在可用时启用 Multi-Node NVLink。**系统或驱动不支持 MNNVL 时会自动禁用。** 该特性**还要求启用 NCCL CUMEM 支持**（`NCCL_CUMEM_ENABLE`）。**MNNVL 要求构成 NVLink 域的所有节点有一个完全配置且正常运行的 IMEX 域**（IMEX 域细节见 CUDA 文档，诊断见 `38`）。取值：`0` 禁用；`1` 启用（**不支持或无法启用时初始化失败**）；`2` 自动探测（**不支持或资源无法分配时不失败**） |
| `NCCL_MNNVL_UUID` | 2.25 | 把 MNNVL UUID 设为用户定义的值，该值会被赋给 128 位 UUID 的高 64 位和低 64 位。**正常情况下 MNNVL UUID 由 Fabric Manager 分配，不需要覆盖。** 取 64 位整数 |
| `NCCL_MNNVL_CLIQUE_ID` | 2.25 | 把 MNNVL Clique Id 设为用户定义的值。正常由 Fabric Manager 分配，但**本变量可用于"软"划分 MNNVL 作业**——**NCCL 只把 `<UUID, CLIQUE_ID>` 相同的 rank 视为同一 NVLink 域的成员**。取 32 位整数 |

## RAS

（详见 `40-troubleshooting-ras.md`）

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_RAS_ENABLE` | 2.24 | 启用 RAS 子系统。**默认 1（启用）**，设 0 禁用 |
| `NCCL_RAS_ADDR` | 2.24 | RAS 监听客户端连接的 socket 的 IP 地址和端口。**RAS 可以在多个进程间共享这个 socket，但当多个独立 NCCL 作业共享同一节点时这并不理想**（且若这些作业属于不同用户，OS 不会允许共享 socket）。**这种情况下每个作业应以不同值启动**（如 `localhost:12345`、`localhost:12346`）。**由于通常用 `localhost`，只有能访问作业所在节点的人才能连接。** 也可以指定一个外部可访问网络接口的地址，**使 RAS 可从其他节点（如集群头节点）访问，但这有需要考虑的安全影响**。**默认 `localhost:28028`**。第一部分可用主机名或 IP，**IPv6 地址需用方括号括起**（如 `[::1]`） |
| `NCCL_RAS_TIMEOUT_FACTOR` | 2.24 | 应用于 RAS 所有超时的**乘数因子**。RAS 依赖多个 5 到 60 秒范围的超时来判断应用状态和维护内部通信，**不同超时之间有复杂的相互依赖**。本变量可以**安全、一致地按比例放大所有这些超时**，适用于默认值偏小的情况，例如 NCCL 应用受到高开销的调试/跟踪影响、执行变得不可预测时。**此时若还想用 `ncclras` 客户端，可能也需要增大（或禁用）它的超时。** 默认 1 |

## 对称 kernel

| 变量 | 起始版本 | 说明 |
| --- | --- | --- |
| `NCCL_SYM_GIN_KERNELS_ENABLE` | 2.29U1 | 启用使用 GIN 做网络通信的对称 kernel。**默认 1（启用）**，设 0 禁用 |

---

# 常用变量速查

| 目的 | 变量 |
| --- | --- |
| 指定 bootstrap / socket 网口 | `NCCL_SOCKET_IFNAME` |
| 指定 IB HCA | `NCCL_IB_HCA` |
| 强制网络后端 | `NCCL_NET`（`IB` / `Socket` / 插件名） |
| 开日志 | `NCCL_DEBUG`、`NCCL_DEBUG_SUBSYS`、`NCCL_DEBUG_FILE` |
| 隔离 P2P 问题 | `NCCL_P2P_DISABLE`、`NCCL_P2P_LEVEL` |
| 隔离 IB 问题 | `NCCL_IB_DISABLE` |
| IB 超时不够 | `NCCL_IB_TIMEOUT` + `NCCL_IB_RETRY_CNT`（乘积） |
| RoCE 路由熵不足 | `NCCL_IB_QPS_PER_CONNECTION` |
| 限制 GPU 资源占用 | `NCCL_MAX_CTAS`、`NCCL_CTA_POLICY` |
| 计算通信 overlap | `NCCL_CTA_POLICY=EFFICIENCY` 或 `ZERO`、`NCCL_MEM_SYNC_DOMAIN` |
| 内存紧张 | `NCCL_BUFFSIZE`、`NCCL_NET_SHARED_BUFFERS` |
| 容器内 shm/NUMA 问题 | `NCCL_CUMEM_HOST_ENABLE=0` |
| 容器内 IPC 不通 | `NCCL_IPC_USE_ABSTRACT_SOCKET=0` |
| 多 communicator 防死锁 | `NCCL_LAUNCH_ORDER_IMPLICIT=1` |
| 启用网络 buffer 注册 | `NCCL_PXN_DISABLE=1`（PXN 与之互斥） |
| 查作业健康 | `NCCL_RAS_ENABLE`、`NCCL_RAS_ADDR` |
