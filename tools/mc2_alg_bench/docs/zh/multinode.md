# MC2算法性能工具跨机使用指南

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

## 创建Hostfile

在工具目录创建文件，将`server-a`和`server-b`替换为实际主机名或IP，第一项必须是启动节点：

```bash
cat > hosts_2x8 <<'HOSTS'
server-a:8
server-b:8
HOSTS
```

两行各表示该节点启动8个进程。所有节点的设备数须与`-p 8`一致。

## 检查进程启动

先检查MPI启动与结果归集，不执行NPU通信：

```bash
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 \
  --check-topology --output-dir /data/mc2_runs/topology_2x8_01
```

将输出路径改为可写的新目录。预期16个Rank全部成功并归集完整；失败时先处理SSH、MPI、路径或权限问题。该检查通过不代表设备通信网络已通过验证。

## 查询跨机候选

先选择实际主机通信网卡，以下`eth0`须替换为两端适用的网卡名：

```bash
export HCCL_SOCKET_FAMILY=AF_INET
export HCCL_SOCKET_IFNAME=eth0
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 --query-only -- \
  --op allreduce -a aicpu_ts
```

仅查询会建立真实通信域，终端列出候选，结果保存为`query_summary.json`。切换到CCU查询时使用`-a ccu_sched`；一个拓扑的候选不能直接用于推断其他拓扑。

## 执行两机测试

把`sole[mesh]`替换为当前跨机查询返回的完整算法名：

```bash
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K --alg 'sole[mesh]'
```

CCU使用相同入口：

```bash
bash run_alg_bench.sh -n 16 -p 8 --hostfile ./hosts_2x8 -- \
  --op allreduce -a ccu_sched -b 32K -e 32K --alg 'sole[mesh]'
```

默认流程自动构建、分发、安装算子包，执行测量并汇总各节点数据。仅在两端已有匹配包和测试程序缓存时，在`--`前加`--skip-build`；更新SDK、源码或测量模式后先正常构建。切换到CCU也须先构建其独立包。

指定算法未命中时跳过，省略`--alg`则测试候选并集。不要把AllToAllVWrite直接套用到该16Rank示例，须先核对安装SDK的支持条件。

## 检查结果

终端打印本轮目录。先确认`Cluster PASS: 16/16 successful ranks`，再检查`report/report.csv`有性能值、`summary.json`的`measured > 0`且没有作业失败，默认采集的解析审计也须通过。

| 问题 | 优先检查 |
| --- | --- |
| 远端进程未启动或文件未回收 | SSH、rsync、Hostfile、目录权限与MPI环境 |
| 通信域建立失败或超时 | 主机网卡选择、设备网络、HCCL配置及两端SDK |
| 一侧包部署或加载失败 | 安装目录权限、动态库、包模式及构建日志 |
| 缺Rank或性能解析失败 | `cluster_summary.json`、各Rank日志及`profile_analysis/profiling_audit.json` |

启动器默认执行超时为600秒，可在`--`前用`--timeout`调整。失败时保留整轮目录，先定位首条错误再重试。
