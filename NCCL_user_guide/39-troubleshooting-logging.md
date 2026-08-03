# 39 · Troubleshooting · 日志

> 原文：Troubleshooting → Logging

NCCL 提供可配置的日志，用于诊断问题、理解运行时行为，以及了解 NCCL 执行期间做出的选择（算法选择、拓扑检测、网络配置等）。

## 日志相关环境变量

| 变量 | 作用 |
| --- | --- |
| `NCCL_DEBUG` | 设置详细程度：VERSION、WARN、INFO、TRACE |
| `NCCL_DEBUG_SUBSYS` | 按子系统过滤日志输出 |
| `NCCL_DEBUG_FILE` | 把日志写到文件（`%h` 主机名，`%p` PID） |
| `NCCL_DEBUG_TIMESTAMP_FORMAT` | 设置时间戳格式（strftime 语法） |
| `NCCL_DEBUG_TIMESTAMP_LEVELS` | 选择哪些级别带时间戳 |

## 日志级别

从最不详细到最详细：

| 级别 | 内容 | 使用场景 |
| --- | --- | --- |
| VERSION | 启动时打印 NCCL 版本 | 验证安装 |
| WARN | 警告和错误 | **生产环境的最低配置** |
| INFO | 详细的运行信息 | 诊断运行时问题 |
| TRACE | 可重放的 trace，外加 CALL API | 深度调试 / NCCL 开发 |

设置方式：

```bash
NCCL_DEBUG=INFO ./my_app
```

输出形态示例（具体值、行号和格式随版本和环境而变）：

- `VERSION` 输出一行版本号，形如 `NCCL version 2.30.3+cuda13.0`
- `WARN` 输出形如 `[时间戳] 主机:PID:TID [设备号] 文件:行号 NCCL WARN <消息>`
- `INFO` 输出形如 `主机:PID:TID [设备号] NCCL INFO <消息>`
- `TRACE` 配 `NCCL_DEBUG_SUBSYS=CALL` 输出形如 `主机:PID:TID NCCL CALL ncclAllReduce(参数...)`

## 按子系统过滤

用 `NCCL_DEBUG=INFO` 或 `TRACE` 时，可以用 `NCCL_DEBUG_SUBSYS` 过滤到特定子系统，避免被无关消息淹没。

```bash
# 只看网络相关消息
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=NET ./my_app

# 看多个子系统
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=INIT,NET,GRAPH ./my_app

# 看所有内容，但排除啰嗦的 proxy 消息（^ 前缀表示排除）
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=^PROXY ./my_app

# 排除多个子系统
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=^PROXY,ALLOC ./my_app
```

### 可用子系统

| 子系统 | 覆盖内容 |
| --- | --- |
| `INIT` | 初始化与 setup |
| `COLL` | 集合操作（AllReduce、Broadcast 等） |
| `P2P` | 点对点 send/receive 操作 |
| `SHM` | 共享内存 transport |
| `NET` | 网络 transport（IB、socket 等） |
| `GRAPH` | 拓扑检测与图搜索 |
| `TUNING` | 算法与协议选择 |
| `ENV` | 环境变量处理 |
| `ALLOC` | 内存分配 |
| `CALL` | 函数调用跟踪 |
| `PROXY` | proxy 线程操作 |
| `NVLS` | NVLink SHARP 操作 |
| `BOOTSTRAP` | 早期初始化与 bootstrap |
| `REG` | Buffer 注册 |
| `PROFILE` | 粗粒度的初始化 profiling |
| `RAS` | 可靠性、可用性、可服务性 |
| `DESTROY` | communicator destroy/abort/revoke 以及插件卸载/关闭操作 |
| `ALL` | 全部子系统 |

**默认子系统（未设 `NCCL_DEBUG_SUBSYS` 时）是 `INIT,BOOTSTRAP,ENV`。**

### 各子系统的典型输出内容

| 子系统 | 日志里能看到什么 |
| --- | --- |
| `INIT` | 插件初始化与分配（NET/GIN/RMA 插件）、使用的网络、DMA-BUF 可用性、`Init START` 标记、GPU/CPU 亲和性 |
| `GRAPH` | 检测到的 Tree 和 Ring 结构，形如 `Tree 0 : -1 -> 0 -> 1/-1/-1`、`Ring 00 : 1 -> 0 -> 1` |
| `COLL` | 每次集合操作的 opCount、send/recv buffer 地址、count、datatype、op、root、nranks、stream |
| `SHM` | `/dev/shm` 中 host buffer 的 MMAP 分配，以及走 SHM 的 channel，形如 `Channel 00 : 0[0] -> 1[1] via SHM/direct/direct` |
| `RAS` | RAS 线程启动、addRanks 请求处理、peer 数量变化 |
| `DESTROY` | 各 rank 的 `Destroy COMPLETE`，以及 env 插件关闭 |
| `PROXY` | proxy 连接建立、`proxyProgressAsync`、opId 匹配、操作发起结果 |
| `ENV` | 哪些环境变量被设置成了什么值，形如 `NCCL_SHM_DISABLE set by environment to 1.` |
| `REG` | buffer 注册的 comm、地址、大小 |
| `ALLOC` | Cuda Host Alloc、MemManager 初始化、memory stack、Realloc、`Cuda CallocAsync` 等各类分配 |
| `CALL` | 每个 NCCL 函数调用及其参数（**需要 `NCCL_DEBUG=TRACE`**） |
| `BOOTSTRAP` | bootstrap 各阶段耗时（create/send/recv/ring/delay） |
| `PROFILE` | bootstrap 耗时，外加 init 各阶段耗时分解（kernels/alloc/bootstrap/allgathers/topo/graphs/connections/rest） |
| `NET` | proxy 连接、走网络的 channel，形如 `Channel 00/0 : 0[0] -> 1[1] [send] via NET/Socket/0` |
| `P2P` | shareable buffer 分配、走 P2P 的 channel，形如 `Channel 00/0 : 0[0] -> 1[1] via P2P/CUMEM` |
| `TUNING` | **每次操作选中的算法和协议**，形如 `AllReduce: 33554432 Bytes -> Algo RING proto SIMPLE channel {Lo..Hi}={0..31}` |
| `NVLS` | Multicast group 的创建、分配结果（ucptr/mcptr、granularity、size） |

## 输出到文件

多进程作业下往终端打日志会很乱。**建议用 `NCCL_DEBUG_FILE` 按进程和主机名分文件写**，便于隔离和分析特定 rank 的问题：

```bash
NCCL_DEBUG=INFO NCCL_DEBUG_FILE=/tmp/nccl_%h_%p.log ./my_app
```

格式说明符：

- `%h` —— 替换为主机名
- `%p` —— 替换为进程 ID（PID）

上例会为每个进程生成形如 `/tmp/nccl_node-01_12345.log` 的文件。

**注意：确保文件名模式在所有进程间唯一，否则文件会被写坏。**

## 时间戳配置

### 格式

用 `NCCL_DEBUG_TIMESTAMP_FORMAT` 自定义格式（strftime 语法）：

```bash
# 默认格式：[YYYY-MM-DD HH:MM:SS]
NCCL_DEBUG=INFO NCCL_DEBUG_TIMESTAMP_LEVELS=INFO ./my_app

# 带毫秒
NCCL_DEBUG=INFO NCCL_DEBUG_TIMESTAMP_LEVELS=INFO NCCL_DEBUG_TIMESTAMP_FORMAT="[%F %T.%3f] " ./my_app

# 关闭时间戳
NCCL_DEBUG=INFO NCCL_DEBUG_TIMESTAMP_LEVELS=INFO NCCL_DEBUG_TIMESTAMP_FORMAT="" ./my_app
```

### 级别

用 `NCCL_DEBUG_TIMESTAMP_LEVELS` 控制哪些级别带时间戳：

```bash
# 给 WARN、INFO、TRACE 都加时间戳
NCCL_DEBUG=INFO NCCL_DEBUG_TIMESTAMP_LEVELS=WARN,INFO,TRACE ./my_app

# 除 TRACE 之外都加
NCCL_DEBUG=TRACE NCCL_DEBUG_TIMESTAMP_LEVELS=^TRACE ./my_app
```

**默认只有 WARN 消息带时间戳。**

## 常见调试场景

### 初始化 hang

```bash
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=INIT,BOOTSTRAP,NET ./my_app
```

关注：bootstrap 连接问题、网络接口选择问题、rank 同步失败。

### 网络问题（超时、连接失败）

```bash
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=NET ./my_app
```

关注：选中了哪些网络接口和设备、连接建立消息、网络 transport 的错误消息。

### 理解拓扑检测

```bash
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=GRAPH ./my_app
```

可以看到：检测到的 GPU 及其互连、NVLink 和 PCIe 拓扑、网络设备的局部性。

### 性能问题

```bash
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=TUNING ./my_app
```

可以看到：选中的算法（Ring、Tree 等）、协议选择（Simple、LL、LL128）、channel 和线程配置。

### 跟踪 NCCL API 调用

```bash
NCCL_DEBUG=TRACE NCCL_DEBUG_SUBSYS=CALL ./my_app
```

### 完整调试会话

```bash
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=ALL NCCL_DEBUG_FILE=/tmp/nccl_%h_%p.log ./my_app
```
