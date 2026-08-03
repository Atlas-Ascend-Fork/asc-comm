# 38 · Troubleshooting · 性能与调优

> 原文：Troubleshooting → Performance and tuning

## 性能问题的定位思路

性能问题成因多样，可能针对特定应用或特定硬件。**关键是先把「NCCL 性能 bug」和「配置或硬件问题」区分开。**

两个测量工具：

| 工具 | 能力 |
| --- | --- |
| [nvbandwidth](https://github.com/NVIDIA/nvbandwidth) | 测 GPU 显存带宽和 GPU 到 GPU 带宽（NVLink 或 PCIe）。**编译时带 multinode 支持**还能测节点间 MNNVL 带宽 |
| [nvloom](https://github.com/NVIDIA/nvloom) | 功能与 nvbandwidth 类似，**额外提供 NVLink SHARP 系统的 benchmark** |

## 节点内通信

nvbandwidth 默认编译为测试节点内通信。要注意的问题是**节点拓扑和 NVLink 问题**。

```bash
nvidia-smi topo -m          # 查看节点内拓扑，据此判断各组件间的预期带宽
nvidia-smi topo -p2p n      # 验证 GPU 之间能否通过 NVLink 直接通信
```

有了这些信息，就可以用 nvbandwidth 验证 GPU 两两之间能否达到预期带宽（经 PCIe 或 NVLink）。

nvloom 的对应测试：

```bash
srun/mpirun -n <number of processes> ./nvloom -s pairwise  --sizeMin 1M --sizeMax 4G
srun/mpirun -n <number of processes> ./nvloom -s multicast --sizeMin 1M --sizeMax 4G
```

## 节点间通信

要 profile 节点间性能，nvbandwidth 必须带 multinode 支持编译：

```bash
cmake -DMULTINODE=1 .
make
```

编译后可以测网络的带宽和延迟：

```bash
srun/mpirun -n <number of processes> ./nvbandwidth -p multinode
```

这会用给定进程数运行以 `multinode` 为前缀的测试。

nvloom 的等价测试：

```bash
srun/mpirun -n <number of processes> ./nvloom -p gpu-to-rack rack-to-rack fabric-stress --sizeMin 1M --sizeMax 4G
```

**nvbandwidth（或 nvloom）报告的带宽低于预期峰值，说明 GPU 间通信有问题。** 一个可能原因是 GPU 之间没有 NVLink 连接——用 `nvidia-smi topo -p2p n` 验证。

测 fabric 性能用 `ib_write_bw` 和 `ib_write_lat`，见 `36-troubleshooting-networking.md`。

**判断准则**：如果 nvbandwidth / nvloom 和 `ib_write_bw` 的结果都符合硬件预期，**但 NCCL 性能低于预期，那就可能是 NCCL 配置次优**，转到下文的调优部分。

## 多节点 NVLink（MNNVL）问题

**若 MNNVL 可用，NCCL 会用它做同一 NVLink 域内的节点间通信。** 如果它没被使用、且用户也没禁用（即 `NCCL_MNNVL_ENABLE` 未设为 0），那可能是底层服务有问题。

诊断要用 **Internode Memory Exchange（IMEX）服务**。用 `nvidia-imex-ctl` 检查 IMEX 域状态（**要求域内每个节点都在运行 IMEX 守护进程，可能需要 sudo 权限**）：

```bash
nvidia-imex-ctl -H -N
```

**先看输出中的 `Domain State` 一行。** 若不是 `UP`，就要检查域内每个节点的状态和它们的连通性矩阵。常见成因：

| 现象 | 含义 |
| --- | --- |
| status `UNAVAILABLE`，连通性矩阵显示 `I`/`N`/`D` | 节点 down |
| status `READY`，连通性矩阵显示 `V` | **节点之间驱动版本不匹配** |

**健康的 IMEX 域应该是：域内所有节点处于 `READY` 状态，连通性矩阵显示 `C`。**

### 权限相关的警告

NCCL 有时会报：

```
transport/p2p.cc:XXX NCCL WARN Cuda failure 800 'operation not permitted'
```

**这可能表示当前用户对位于 `/dev/nvidia-caps-imex-channels/channel*` 的 IMEX 安全文件没有写权限。** 修改这些文件的权限以允许写入即可解决。

### 无 IMEX 守护进程访问权限时的替代方案

```bash
nvidia-smi -q | grep -v GUID | grep -A4 Fabric
```

用它检查 NVLink fabric 状态，并**验证 NVLink 域内所有节点的 `cliqueId` 是否相同**。

## 调优 NCCL 配置

NCCL 已针对广泛的系统做过调优，且每个新版本会重新调优。但有些边缘情况下换个设置会更好。

> **注意：一般不建议在生产环境使用这些变量**，因为在某个 benchmark 场景下的调优收益可能在别处导致次优设置。

最常用的调优参数：

| 变量 | 作用与权衡 |
| --- | --- |
| `NCCL_MIN_CTAS` / `NCCL_MAX_CTAS` | 增加 CTA 数会**消耗更多 GPU 资源**，但可能提高吞吐 |
| `NCCL_CHUNK_SIZE` | 控制 `ncclSend`/`ncclRecv` 和 AlltoAll 操作通过网络发送的消息大小。**增大它在延迟受限的情况下可能提升带宽** |
| `NCCL_IB_QPS_PER_CONNECTION` | 每个连接的 QP 数，**默认 1**。在**启用了 ECMP 路由或每 NIC 多端口**的系统上，增大该值可以改善网络路径多样性、提高吞吐 |
| `NCCL_CROSS_NIC` | 控制 NCCL 是否允许 ring 和 tree 使用不同的 NIC（使节点间通信在不同节点上使用不同 NIC）。**强制 cross-NIC 通信在未优化的 rail 配置下可能提升性能，但可能在其他网络上造成拥塞。默认值 2** |

### RoCE 注意事项

**在 RoCE fabric 上，每连接使用多个 QP 往往是达到最优性能的必要条件。**

## 进程/线程亲和性

**错误的进程和线程放置会严重影响性能。**

- `NCCL_IGNORE_CPU_AFFINITY` 环境变量让 NCCL 根据 GPU 亲和性来为它创建的线程分配 CPU 亲和性。
- **但进程放置在用户手里**，且要求随节点架构而变。可以通过 `numactl`、OpenMPI 的 `--bind-to`、`srun` 的 `--cpu-bind` 选项或 machine file 选项来设置 CPU 亲和性。

**一般规则**：依据 `nvidia-smi topo -m` 提供的信息，按该工具报告的 GPU-CPU 亲和性把进程分散开。

## 分诊顺序速查

1. `nvidia-smi topo -m` / `-p2p n` —— 确认拓扑和 NVLink 可达性
2. nvbandwidth / nvloom —— 确认 GPU 间实际带宽达到硬件预期
3. `ib_write_bw` / `ib_write_lat` —— 确认 fabric 带宽和延迟正常
4. 前三步都正常但 NCCL 慢 → 才动 NCCL 调优旋钮
5. MNNVL 没被用上 → `nvidia-imex-ctl -H -N` 查 IMEX 域
6. 别忘了进程亲和性——这一项经常被忽略但影响很大
