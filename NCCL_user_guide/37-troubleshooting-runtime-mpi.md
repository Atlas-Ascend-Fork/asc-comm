# 37 · Troubleshooting · 运行时与 MPI

> 原文：Troubleshooting → Runtime and MPI issues

## 错误处理

始终确认 NCCL 调用的返回码等于 `ncclSuccess`。**任何调用失败并返回非 `ncclSuccess` 时，设 `NCCL_DEBUG=WARN` 会让 NCCL 在返回错误前打印明确的警告消息。**

错误分两大类：

| 类别 | 含义 |
| --- | --- |
| `ncclUnhandledCudaError`、`ncclSystemError` | 对**外部库**的调用失败 |
| `ncclInvalidArgument`、`ncclInvalidUsage` | 使用 NCCL 的应用中存在**编程错误** |

两种情况都应参考 NCCL 的警告消息来定位如何解决。

## 内存问题

### 共享内存

NCCL 为进程间（甚至同进程线程间）通信创建共享内存段，传统上位于 `/dev/shm`。**操作系统对这些资源的限制可能需要相应提高。**

共享内存不足时 NCCL 初始化会失败，`NCCL_DEBUG=WARN` 下可见类似消息：

```
NCCL WARN Error: failed to extend /dev/shm/nccl-03v824 to 4194660 bytes
```

#### Docker

**Docker 容器默认限制共享内存和 pinned 内存资源。** 在容器内用 NCCL 时要调整共享内存大小，例如给 docker 启动命令加：

```
--shm-size=1g --ulimit memlock=-1
```

#### Systemd

用 `mpirun` 或 SLURM 跑作业时，**systemd 检测到对应用户未登录时可能会清理共享内存中的文件**（本意是清理旧临时文件），导致 NCCL 在 init 期间崩溃：

```
NCCL WARN unlink shared memory /dev/shm/nccl-d5rTd0 failed, error: No such file or directory
```

原因是 `mpirun` 和 SLURM 作业可以在 systemd 看不到用户登录的情况下在节点上运行。**系统管理员需要禁用该清理机制**（清理工作可以改由 SLURM epilogue 脚本完成）。在 `/etc/systemd/logind.conf` 中设置：

```
RemoveIPC=no
```

改完后重启守护进程：

```bash
sudo systemctl restart systemd-logind
```

#### cuMem host allocations

版本演进：

| 版本 | 行为 |
| --- | --- |
| **2.23 起** | 支持用 cuMem host allocation 作为替代的共享内存机制 |
| **2.24 起** | 若 CUDA driver ≥ 12.6 且 CUDA runtime ≥ 12.2，**默认启用**它而非 `/dev/shm` |
| **2.26.5 起** | NCCL 会**检查 cuMem host allocation 是否可用**，必要时自动回退到 `/dev/shm` 代码路径 |

**关键依赖**：cuMem host allocation 依赖正确配置且工作正常的 **NUMA 支持**，而某些 VM 和容器化场景下不具备。**特别是 Docker 默认禁用 NUMA 支持**（可以用 `--cap-add SYS_NICE` 启用）。

2.26.5 之前的版本，可以手动指定 `NCCL_CUMEM_HOST_ENABLE=0` 达到同样的回退效果。**但仍推荐把底层系统配置好让 cuMem host allocation 能工作，因为它在 communicator abort 期间可靠性更好。**

两个已知坑：

- 在**没有 CUDA P2P 连通性**的系统上，若 CUDA driver 版本早于 13.0，cuMem host allocation 可能失败。
- **CUDA Forward Compatibility 特性会影响 NCCL 准确判断当前驱动版本**，导致在本不该启用的旧驱动上启用了 cuMem host allocation。

遇到问题就用 `NCCL_CUMEM_HOST_ENABLE=0` 关掉该特性。

### 栈大小

NCCL 的图搜索算法**高度递归**，尤其在 MNNVL 系统上（许多 rank 可通过 CUDA P2P 到达），**创建 communicator 期间可能临时需要超过 2 MB 的线程栈**。

**关键反直觉点**：Linux 默认栈限制（8 MB）已知足够，但**把限制改成 `unlimited` 反而会崩溃**。原因是 GNU libc 的一个特性（见 `pthread_create(3)` man page）：这种设置会导致 **NCCL 后台线程的栈大小反而降到只有 2 MB**，可能不够用。

处理办法：

```bash
ulimit -s          # 打印当前限制
ulimit -s 8192     # 需要时重置为 8192 KB
```

启动多节点 NCCL 作业时还要确保新设置传播到其他节点。

**2.28 起**，NCCL 会查询新启动线程的默认栈大小，必要时改成对当前作业安全的值。但仍建议受影响系统的用户设法修正系统级设置——这个配置无论初衷多好，**都是一个可能影响范围超出 NCCL 作业的严重误配**。

### Unified Memory（UVM）

**2.23 起**，NCCL 利用 CUDA memory pool 来优化 graph capture，该特性**依赖 UVM 可用**。

某些虚拟机环境下 UVM 可能默认不开启，通常可以通过配置更改启用。

### 文件描述符

NCCL 在大规模运行时使用相当多的文件描述符，限制可能需要提高。**例如一个 144-rank 作业使用 16 个 GIN context，每进程可能需要超过 32K 个文件描述符。**

系统级限制：

```bash
cat /proc/sys/fs/file-max
```

默认值常在百万级，systemd 可能设得更高。**但若该限制被人为降低了**（例如被 `/etc/sysctl.d/` 下某个文件），可能需要重新提高：

```bash
sysctl -w fs.file-max=2097152
```

每进程限制用 `ulimit -n` 查询。要永久提高，在 `/etc/security/limits.d/` 下新建文件（或编辑已有的），加一行：

```
* - nofile 131072
```

这会把所有用户的 soft 和 hard 限制都设为 128K。

**注意**：提高系统级限制或每进程 hard 限制需要系统管理员操作。

## MPI

用 MPI 跑 NCCL 之前（例如 `mpirun <my_application>`），**先跑个简单的 MPI 测试确认节点之间能正常通信**。分两步：

第一步，确认应用能并行启动：

```bash
# Open MPI-based implementations:
mpirun -np <number of processes> -N <processes per node> "hostname"

# MPICH-based implementations:
mpirun -np <number of processes> -ppn <processes per node> "hostname"
```

第二步，确认 MPI 能初始化并跑一个简单归约：

```bash
wget https://raw.githubusercontent.com/pmodels/mpich/main/examples/cpi.c
mpicc -o cpi cpi.c
mpirun -np <number of processes> -N <processes per node> ./cpi
```

### 基于 Open MPI 的 MPI（如 NVIDIA HPC-X）

许多基于 NCCL 的应用编译时链接 MPI，以利用它的并行启动器和启动期的广播机制。**集群环境中若 MPI 配置不正确，`mpirun` 可能启动失败、hang 或报错。**

两个前提说明：

- 下面这些设置假定环境会自动把变量转发到各 MPI rank（例如 SLURM 集群）。**不确定的话就用 `mpirun -x VARIABLE_NAME=<variable_value>` 显式转发，而不是 `export`。**
- 这些设置**不会影响 NCCL 性能**；但如果 MPI 也被频繁用于通信，则可能影响应用性能。

#### 网络接口选择

**若应用在启动时 hang，或在 `libmpi.so` 中报段错误，可能是 MPI 选错了网络接口。**

列出活动且已连接的接口：

```bash
ip -br link | grep LOWER_UP | grep ' UP '
```

通常只有一部分接口（如 `eth*`、`en*`、`ib*`）连到了网络；loopback（`lo`）和容器相关接口一般不适用。

**如果管理员指定了 `NCCL_SOCKET_IFNAME`，让 MPI 用同一个接口**：

```bash
export OMPI_MCA_btl_tcp_if_include=<interface-name>
```

或者反向排除那些通常没连到网络的接口：

```bash
export OMPI_MCA_btl_tcp_if_exclude=lo,docker0,virbr0
```

**注意：include 和 exclude 两个选项不要同时使用。**

#### PMIx Data Store 选择

Open MPI 的某个 PMIx 组件过去有一个问题（[ompi#7516](https://github.com/open-mpi/ompi/issues/7516)），现已修复，但**基于较老版本的 MPI 栈仍可能遇到**。若应用报类似错误：

```
PMIX ERROR: ERROR in file gds_ds12_lock_pthread.c
```

可以强制换一个 GDS 组件：

```bash
export PMIX_MCA_gds=hash
```

#### UCX 与 HPC-X

HPC-X 常用 UCX 库。遇到 UCX 警告：

```
UCX WARN network device 'XXX' is not available, please use one or more of: YYY, ...
```

显式指定设备：

```bash
export UCX_NET_DEVICES=YYY
```

遇到 UCX 错误：

```
UCX ERROR no active messages transport to <no debug data>: Unsupported operation
Error: Failed to resolve UCX endpoint
```

尝试简化 UCX transport 选择：

```bash
export UCX_TLS=self,sm,tcp
```

必要时可以禁用 UCX 组件、退回基础 TCP 通信：

```bash
export OMPI_MCA_pml=^ucx
export OMPI_MCA_coll_hcoll_enable=0
export OMPI_MCA_coll=^ucc
export OMPI_MCA_btl=self,tcp
```

## 排查清单

| 症状 | 检查方向 |
| --- | --- |
| init 失败，提示 `/dev/shm` 扩展失败 | 共享内存限制；Docker 加 `--shm-size` |
| init 期间 unlink 共享内存失败 | systemd 的 `RemoveIPC=no` |
| 容器/VM 内 cuMem 相关失败 | NUMA 支持；`--cap-add SYS_NICE`；或 `NCCL_CUMEM_HOST_ENABLE=0` |
| 创建 communicator 时崩溃（尤其 MNNVL） | `ulimit -s` 是否被设成 `unlimited`，改回 8192 |
| 大规模作业资源耗尽 | `ulimit -n` 与 `/proc/sys/fs/file-max` |
| graph capture 相关问题 | UVM 是否启用 |
| 启动 hang 或 `libmpi.so` 段错误 | MPI 网络接口选择 |
| `PMIX ERROR ... gds_ds12` | `PMIX_MCA_gds=hash` |
| UCX 警告/错误 | `UCX_NET_DEVICES`、`UCX_TLS`，必要时退回 TCP |
