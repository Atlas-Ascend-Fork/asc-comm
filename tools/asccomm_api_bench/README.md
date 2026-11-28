# AscComm API性能测试工具

简体中文 | [English](./README_en.md)

`asccomm_api_bench`用于查询当前拓扑支持的MC2通信算法，并选择算法在NPU上测试性能。MC2是通信与计算协作的一种实现方式；NPU是神经网络处理器。工具自动完成算子包构建安装、测试程序编译、多进程执行、设备性能数据采集和报告生成。

## 支持范围

内置性能测试支持多种数据类型，默认`fp16`（16位浮点），适用于配套SDK支持的Ascend 950PR/Ascend 950DT环境。各引擎的类型范围见《[使用说明](./docs/zh/usage.md)》。

| 算子 | `aicpu_ts`引擎 | `ccu_sched`引擎 |
| --- | --- | --- |
| `allgather`、`allreduce`、`reducescatter`、`alltoall`、`alltoallv` | 支持测试 | 支持测试 |
| `alltoallvwrite` | 不提供 | 支持测试 |

具体算法、归约类型和拓扑是否可用，以安装SDK的查询结果与实际执行为准。

## 快速开始

尚未下载源码时，先获取并进入工具目录：

```bash
git clone https://gitcode.com/cann/asc-comm.git
cd asc-comm/tools/asccomm_api_bench
```

已有源码时直接进入`tools/asccomm_api_bench`目录。随后按《[使用指南](./docs/zh/usage.md#配置环境)》准备并加载CANN和MPI环境，工具不会自动安装这些依赖。

环境就绪后，首次测试使用两张设备、32KiB的AICPU AllReduce；MPI启动两个参与通信的进程，每个进程称为一个Rank。

```bash
bash run_alg_bench.sh -n 2 -p 2 \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

该命令自动查询32KiB下的候选并逐个实测，无需预先查询或填写算法名。构建、安装、执行和报告由工具自动完成。

## 查看结果

终端打印本轮`Run directory:`和性能表，结果默认写入`build_out/runs/run-UUID/`。

- 性能测试：查看`report/report.csv`和`report/summary.json`
- 执行失败：保留本轮目录，按终端提示查看构建日志或各Rank日志

看到性能表、`measured > 0`、适用指标有值且解析成功，才说明本轮获得了性能数据。`Cluster PASS`仅表示进程完成；若显示`NO MEASUREMENTS`，先按使用指南检查查询结果和SDK支持范围。

## 选择算法或更换引擎

只查看相同条件下的候选、不执行测试时使用：

```bash
bash run_alg_bench.sh -n 2 -p 2 --query-only \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

此模式将候选保存到`query_summary.json`，不生成性能CSV。

指定算法时，在实测命令中追加`--alg '完整候选名'`，名称取自相同算子和引擎的查询结果；未命中时跳过实测。完整例子见《[使用指南](./docs/zh/usage.md#查询并选择算法)》。

测试CCU时，将`-a aicpu_ts`改为`-a ccu_sched`。入口自动准备对应引擎的算子包并执行测试。

## 使用文档

- 《[使用指南](./docs/zh/usage.md)》：环境、参数、查询规则、结果与排错
- 《[跨机使用指南](./docs/zh/multinode.md)》：两机16Rank的准备、执行与结果检查

## 贡献与许可

代码贡献遵循《[贡献指南](../../CONTRIBUTING.md)》，资料贡献遵循《[资料贡献指南](../../docs/zh/contributing/doc_contributing.md)》。本工具采用仓库根目录的[CANN许可协议](../../LICENSE)，外部工具保留各自许可。
