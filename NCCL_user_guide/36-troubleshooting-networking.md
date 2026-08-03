# 36 · Troubleshooting · 网络

> 原文：Troubleshooting → Networking Troubleshooting

## IP 网络接口

NCCL **自动探测**用于节点间通信的网络接口。**若某些接口处于 UP 状态但实际无法在节点间通信，NCCL 仍可能尝试使用它们**，从而在 init 函数中失败甚至 hang。

指定使用哪些接口的方法见 `33-environment-variables.md`，重点是 **`NCCL_SOCKET_IFNAME`**。

## IP 端口

NCCL 会打开 TCP 端口来连接进程、交换连接信息。要限制 NCCL 使用的端口范围，可以设置 Linux 内核的 `net.ipv4.ip_local_port_range`。

限制到 50000-51000：

```bash
echo 50000 51000 > /proc/sys/net/ipv4/ip_local_port_range
```

永久生效：

```bash
echo "net.ipv4.ip_local_port_range = 50000 51000" >> /etc/sysctl.conf
```

限制端口范围的用处：**便于在防火墙上开对应范围**。原文给了 Google Cloud 的例子：

```bash
gcloud compute --project=myproject firewall-rules create ncclnet0-ingress --direction=INGRESS --priority=1 --network=ncclnet --action=ALLOW --rules=tcp:50000-51000,22,1024-1039 --destination-ranges=0.0.0.0/0 --target-tags=ncclnet
```

## NIC 级诊断

在 InfiniBand 上跑 NCCL 之前，先做底层网络测试确认端口已 up 且工作在预期配置（link layer、link rate、active 状态）下：

```bash
# 按端口的概要视图
ibstatus

# 按 HCA 和端口的详细视图
ibstat
```

健康链路应满足：

| 检查项 | 期望 |
| --- | --- |
| Port state | **Active**（不是 `Down`、`Init` 或 `Armed`） |
| Physical state | 链路已起，通常是 `LinkUp` |
| Link layer | 是预期的类型（InfiniBand vs RoCE 场景下的 Ethernet），且**所有用于传输的 NIC 保持一致** |
| Rate | 与 fabric 预期一致，**没有意外降速** |

## 带宽测试：ib_write_bw

没有明显网络问题的话，用 `ib_write_bw` 测节点间带宽。选两个计算节点分别作 server 和 client：

```bash
# server 节点
ib_write_bw -d <device> -a

# client 节点
ib_write_bw -d <device> <server_hostname_or_ip> -a
```

`<device>` 换成 `ibstat` 里显示的 HCA 设备名（如 `mlx5_0`）。

**排查 GPUDirect RDMA 时，对比 host 内存与 GPU 内存的结果通常很有用。** 取决于 perftest 的构建方式，GPU 内存选项可能是：

```bash
# GPU memory path
ib_write_bw -d mlx5_0 --use_cuda=<gpu_id> <server_hostname_or_ip> -a

# DMA-BUF path, when supported by the kernel, driver, and perftest build
ib_write_bw -d mlx5_0 --use_cuda=<gpu_id> --use_cuda_dmabuf <server_hostname_or_ip> -a
```

GB300 这类平台在某些 perftest 构建中还可能暴露额外的 data-direct 模式。**对比结果之前，先用 `ib_write_bw --help` 确认你这个构建实际支持哪些 GPU 内存选项。**

## 延迟测试：ib_write_lat

**带宽看起来健康时，尾延迟仍可能很差。** 如果症状像是卡顿、长尾、间歇性变慢，也要测点对点延迟：

```bash
# Server node
ib_write_lat -d <device>

# Client node
ib_write_lat -d <device> <server_hostname_or_ip>
```

**延迟高或不稳定（尤其是带宽也在波动时），可能指示拥塞、重传或链路降级。**

## RDMA 协议统计

```bash
rdma statistic
```

关注测试运行期间是否有以下计数器在增长：

- `rnr_nak_retry_err`
- `packet_seq_err`
- `implied_nak_seq_err`
- `local_ack_timeout_err`

**这些值非零或持续增长，往往说明存在丢包、重试或其他传输层问题。**

## 链路健康：mlxlink

如果症状像是 PCIe、C2C 或物理端口健康度下降，直接查链路：

```bash
mlxlink -d <mst_device>
```

用它确认预期的 link width、link speed、错误状态和整体端口健康。**在带宽或延迟劣化但找不到明显软件配置问题时特别有用。**

## InfiniBand 专项

### Subnet Manager

**InfiniBand fabric 必须有 Subnet Manager（SM）在运行。**

```bash
sudo sminfo
```

若 `sminfo` 失败或显示没有 SM，确保至少有一个节点上跑着 SM（例如 `opensm` 服务）。

### 端口错误计数器

```bash
sudo perfquery -x <lid>
```

检查可能指示线缆或交换机问题的链路错误、symbol 错误、丢包。**以下计数器非零可能意味着硬件或布线问题**：

- `SymbolErrorCounter`
- `LinkErrorRecoveryCounter`
- `LinkDownedCounter`

### 连通性测试

`ibstat` 和 `ibstatus` 没显示问题但怀疑连通性时用 `ibping`。**注意要先在远端节点起 server**：

```bash
# On the remote node, start the ibping server.
sudo ibping -S

# On the local node, ping the server using its LID.
sudo ibping <lid from ibstat>
```

### 全面诊断

更彻底的全 fabric 诊断（通常需要 SM 访问权限）：

```bash
sudo ibdiagnet
```

它检查拓扑、路由，并报告整个 fabric 范围内的错误。

**典型用法**：如果 NCCL 在某些 rail 上正常，却在某个特定 rail 上反复失败或变慢，跑 `ibdiagnet` 并检查生成的报告，看连到那个 rail 的交换机端口是否有链路缺失、坏端口或路由不一致，然后再重试 NCCL 作业。

### 常见 NCCL 错误：pinned memory 不足

IB 上常见的问题是库无法注册足够的 pinned memory，报错形如：

```
NCCL WARN Call to ibv_create_qp failed
```

或：

```
NCCL WARN Call to ibv_reg_mr failed
```

**解决办法是解除注册 pinned memory 的用户限制**，在 `/etc/security/limits.conf`（或你的发行版对应文件）加：

```
* soft memlock unlimited
* hard memlock unlimited
```

**注意**：改 `limits.conf` 通常需要用户注销后重新登录、或重启；对 Slurm 集群，要确保作业是以更新后的 limits 启动的。用 `ulimit -l` 验证新 limits 是否生效。

## RoCE 专项

RoCE fabric 诊断节点间问题需要与 InfiniBand 不同的工具。

### 端口错误计数器

```bash
ethtool -S <nic_name>
```

检查错误、丢包、pause frame 或 PFC 相关计数器。**这些计数器非零可能指示硬件问题、布线问题，或无损 fabric 配置错误。**

### 拥塞指标

**RoCE 的性能问题往往与拥塞控制或无损 fabric 设置有关，而不是彻底的链路故障。**

除 `ethtool` 之外，还要检查你的环境暴露的交换机和 NIC 计数器中关于 PFC、ECN、CNP 或队列丢包的部分。**如果这些计数器在延迟尾部变长或吞吐不稳时增长，先去查拥塞控制和无损 fabric 配置，而不是改 NCCL。**

### 连通性测试

端口级检查没问题但仍怀疑连通性时用 `rping`。同样**要先在远端节点起 server**：

```bash
# On the remote node, start the rping server.
rping -s -a <ip_address_of_the_server_nic> -V -C 10

# On the local node, ping the server using its IP.
rping -c -a <ip_address_of_the_server_nic> -S <ip_address_of_the_client_nic> -V -C 10
```

### 常见 NCCL 错误：GID index 选错

RoCE 上常见问题是为 RoCE v2 NIC 选错了 GID index，报错：

```
NCCL WARN Call to ibv_modify_qp failed with error Invalid argument
```

版本差异很重要：

| NCCL 版本 | 处理方式 |
| --- | --- |
| **2.21 及以后** | GID index **动态选择**，**不应该设** `NCCL_IB_GID_INDEX` |
| 2.21 之前 | 需要跑 `show_gids`，然后把 `NCCL_IB_GID_INDEX` 设为 RoCE v2 GID 对应的 index |

另外，用 RoCE fabric 时可能还需要设 **`NCCL_IB_TC`**，具体取值参考你的厂商文档。

## 工具速查

| 目的 | InfiniBand | RoCE |
| --- | --- | --- |
| 端口状态 | `ibstatus` / `ibstat` | `ethtool -S` |
| 带宽 | `ib_write_bw` | `ib_write_bw` |
| 延迟 | `ib_write_lat` | `ib_write_lat` |
| 传输层统计 | `rdma statistic` | `rdma statistic` + PFC/ECN/CNP 计数器 |
| 链路物理健康 | `mlxlink` | `mlxlink` |
| 连通性 | `ibping` | `rping` |
| Fabric 全局 | `sminfo`、`perfquery`、`ibdiagnet` | 交换机侧工具 |
