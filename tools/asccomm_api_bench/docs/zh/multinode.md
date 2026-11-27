# AscComm API性能测试工具跨机使用指南

简体中文 | [English](../en/multinode.md)

以下以两台各有8张设备的服务器执行16Rank AllReduce为例。Rank是参与通信的进程；MPI负责跨节点启动，HCCL使用设备通信网络传输数据。单机环境、参数和指标说明见《[使用指南](./usage.md)》。

## 准备两台服务器

先完成单机测试，再确认两端满足以下条件：

- 安装兼容的CANN、Host HCCL、MPI及Python，加载对应环境
- 查询头文件、查询库及运行依赖配套，SDK、MPI和运行目录采用相同绝对路径
- 启动节点可通过非交互SSH访问另一节点，SSH和rsync可用
- 两端当前用户有结果目录及算子包安装目录的读写权限
- 设备空闲，主机网络和设备通信网络均已配置并可达

只在第一台机器执行启动命令。工具分发测试程序、部署对应算子包并回收结果，不负责安装CANN、MPI或配置设备网络。

工具在启动节点读取`ASCEND_HOME_PATH`、`MPI_HOME`及按需设置的`HCCL_DIR`，将选定的安装路径统一传给各节点；各节点须安装在相同绝对路径。

## 创建Hostfile

在工具目录创建文件，将`server-a`和`server-b`替换为实际主机名或IP，第一项必须是启动节点：

```bash
cat > hosts_2x8 <<'HOSTS'
server-a:8
server-b:8
HOSTS
```

两行各表示该节点启动8个进程。所有节点的设备数须与`-p 8`一致。

## 查询跨机候选

先选择实际主机通信网卡，以下`eth0`须替换为两端适用的网卡名：

```bash
export HCCL_SOCKET_FAMILY=AF_INET
export HCCL_SOCKET_IFNAME=eth0
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 --query-only \
  --op allreduce -a aicpu_ts
```

仅查询会建立真实通信域，终端列出候选，结果保存为`query_summary.json`。切换到CCU查询时使用`-a ccu_sched`；一个拓扑的候选不能直接用于推断其他拓扑。

## 执行两机测试

把`sole[mesh]`替换为当前跨机查询返回的完整算法名：

```bash
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 \
  --op allreduce -a aicpu_ts -b 32K -e 32K --alg 'sole[mesh]'
```

CCU使用相同入口：

```bash
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 \
  --op allreduce -a ccu_sched -b 32K -e 32K --alg 'sole[mesh]'
```

默认流程自动构建、分发、安装对应引擎的算子包，执行测量并汇总各节点数据。

指定算法未命中时跳过，省略`--alg`则测试候选并集。不要把AllToAllVWrite直接套用到该16Rank示例，须先核对安装SDK的支持条件。

## 检查结果

终端打印本轮目录。先确认`Cluster PASS: 16/16 successful ranks`，再检查`report/report.csv`有性能值、`summary.json`的`measured > 0`且没有作业失败，默认采集的解析审计也须通过。

| 问题 | 优先检查 |
| --- | --- |
| 远端进程未启动或文件未回收 | SSH、rsync、Hostfile、目录权限与MPI环境 |
| 通信域建立失败或超时 | 主机网卡选择、设备网络、HCCL配置及两端SDK |
| 一侧包部署或加载失败 | 安装目录权限、动态库、包模式及构建日志 |
| 缺Rank或性能解析失败 | `cluster_summary.json`、各Rank日志及`profile_analysis/profiling_audit.json` |

失败时保留整轮目录，先定位首条错误再重试。
