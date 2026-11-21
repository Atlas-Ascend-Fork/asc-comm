# MC2算法性能工具使用指南

简体中文 | [English](../en/usage.md)

本指南介绍内置后端的日常使用。算子和引擎范围见《[工具说明](../../README.md#支持范围)》，两机执行见《[跨机使用指南](./multinode.md)》。

## 配置环境

需要匹配的CANN开发包、Host HCCL（主机侧集合通信库）、MPICH兼容的MPI多进程环境，以及Python 3.9+、CMake 3.19+和C++11编译器。各设备须空闲，当前用户须有算子包安装目录的读写权限。

CANN开发包须包含正式查询接口`Mc2GetSupportAlgList`、`Mc2SetCcDataLenRange`及匹配的`libmc2_client.so`。默认profiling（设备性能数据采集）需要SDK提供ACL采集接口和`libmsprofiler.so`；CCU原始数据导出还需要匹配SDK的`msprof`。AICPU直接分析原始采集数据，无需导出时间轴。

在Bash中执行，只需把`CANN_ROOT`、`MPI_HOME`和最后一行的源码路径替换为实际位置。已有这些依赖时不需要重新安装：

```bash
export CANN_ROOT=/usr/local/Ascend/cann
export MPI_HOME=/usr/local/mpi/mpich
if [ -f "${CANN_ROOT}/set_env.sh" ]; then
    source "${CANN_ROOT}/set_env.sh"
else
    source "${CANN_ROOT}/../set_env.sh"
fi
export ASCEND_HOME_PATH="${CANN_ROOT}"
export ASCEND_CANN_PACKAGE_PATH="${CANN_ROOT}"
export PATH="${MPI_HOME}/bin:${PATH}"
cd /path/to/asc-comm/tools/mc2_alg_bench
```

构建优先使用CANN自带的makeself打包工具，再查系统PATH，通常无需单独配置。Host HCCL独立安装时，在启动命令的`--`前增加`--hccl-root /实际安装目录`。标准布局下，CCU所需的`libhcomm.so`会自动链接。

首次使用可直接跳到《[执行单机测试](#执行单机测试)》，先运行AICPU例子并《[查看性能结果](#查看性能结果)》；下方参数和查询说明按需查阅。

## 放置命令参数

命令以第一个`--`分隔启动器参数和测试参数。各部分内一般没有顺序要求，但参数值必须紧跟对应选项。

```text
bash run_alg_bench.sh [启动器参数] -- [测试参数]
```

| 位置 | 参数 | 含义与默认值 |
| --- | --- | --- |
| `--`前 | `-n`、`-p` | 总Rank数、每节点设备数，均默认8；Rank是参与通信的进程 |
| `--`前 | `--query-only` | 只查询候选，不运行性能测试 |
| `--`前 | `--comm-only` | AICPU延迟提交诊断模式，默认关闭 |
| `--`前 | `--skip-build` | 校验并复用已安装包及编译缓存，默认正常构建 |
| `--`前 | `--output-dir` | 新结果目录，省略时自动创建，不能覆盖已有作业 |
| `--`前 | `--no-profiling` | 关闭性能采集解析，仅保留整算子计时和输出校验 |
| `--`前 | `--cann-root`、`--mpi-root`、`--hccl-root` | 显式选择安装目录，否则使用对应环境配置 |
| `--`后 | `--op` | 算子名称，必须指定 |
| `--`后 | `-a` | 引擎：`aicpu_ts`或`ccu_sched`，默认`aicpu_ts` |
| `--`后 | `-d` | 数据类型，内置实测使用`fp32`（32位浮点），默认`fp32` |
| `--`后 | `-o` | 归约类型：`sum`、`prod`、`max`、`min`，默认`sum`；实际支持范围由SDK决定 |
| `--`后 | `-b`、`-e` | 起始和目标结束字节数，默认32KiB和256MiB |
| `--`后 | `-f`或`-i` | 倍增因子或线性字节步长，二者互斥，默认`-f 2` |
| `--`后 | `--alg` | 查询返回的完整算法名，省略时测试全部候选，可重复指定 |
| `--`后 | `--warmup`、`--iterations` | 预热和正式轮数，默认10和20，仅性能测试使用 |

长度支持`B`、`K`、`KB`、`KiB`、`M`、`MB`、`MiB`等后缀，按1024倍数计算。单点须同时设置`-b/-e`；只设置一个端点时，另一个仍使用默认值。倍增扫描包含首个不小于目标结束值的点，线性扫描不超过结束值。默认范围共14个点。

请求字节数不一定等于每Rank的输入和输出大小：AllGather表示接收总量，ReduceScatter表示输入总量，AllToAll类表示每Rank发送总量。后端按Rank数和数据类型核对可均分条件；AllToAllV使用等分数据。

## 查询并选择算法

本节为可选步骤。若需先查看候选再选择算法，使用与首次实测相同的AICPU、两张设备和32KiB条件：

```bash
bash run_alg_bench.sh -n 2 -p 2 --query-only -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

上述命令按32KiB单点筛选。省略`-b/-e`查询当前条件的全部候选，按范围筛选可用`-b 32K -e 128K`。查询规则如下，查询长度称为`dataLen`：

| 用户输入 | 查询行为 |
| --- | --- |
| 指定`--alg`，无论是否指定数据量 | 只以`dataLen=0`查询一次，核对算法名是否命中 |
| 不指定算法和数据量 | 以`dataLen=0`查询当前条件的全部候选 |
| 不指定算法，指定单点 | 将该点字节数传给查询接口 |
| 不指定算法，指定范围 | 按步长逐点查询，算法名去重取并集 |

仅查询仍需要真实设备和通信域，但不构建算子包、不预热、不计时，也不生成性能CSV。结果打印到终端并写入本轮`query_summary.json`。它不能与`--comm-only`、`--analyze`或预热/测量次数参数混用。

性能测试会重新查询，命中才进入实测；未命中提示不支持并跳过，不在工具侧另行兜底。范围查询得到的候选并集在完整测量长度序列上测试，不能把查询返回理解为每个长度都保证执行成功。

需要指定算法时，从相同算子和引擎的查询结果复制完整名称。例如，仅当上面的AICPU查询返回`sole[mesh]`时，才使用：

```bash
bash run_alg_bench.sh -n 2 -p 2 -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K --alg 'sole[mesh]'
```

## 执行单机测试

下面命令均使用两张设备和32KiB单点，不指定算法，由工具自动查询并测试该条件下的候选。首次运行不加`--skip-build`，无需先执行仅查询命令。

AICPU AllReduce：

```bash
bash run_alg_bench.sh -n 2 -p 2 -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

CCU AllReduce：

```bash
bash run_alg_bench.sh -n 2 -p 2 -- \
  --op allreduce -a ccu_sched -b 32K -e 32K
```

CCU AllToAllVWrite：

```bash
bash run_alg_bench.sh -n 2 -p 2 -- \
  --op alltoallvwrite -a ccu_sched -b 32K -e 32K
```

AllToAllVWrite的输入、输出及查询类型均为`fp32`，通信窗口由SDK提供并校验容量。先在单机验证；不要直接套用两机16Rank示例，实际支持范围以SDK查询为准。

其他常规算子只需替换`--op`。入口自动准备对应引擎的算子包、编译测试程序、查询、预热、测量、校验并生成报告，无需额外构建或解析命令。每个测试项在正式轮结束后校验末次输出。

重复运行会创建新的结果目录，无需删除之前的记录。相同配置可在`--`前加`--skip-build`；源码、SDK、算子包、测量模式或采集开关变化后，先正常重建。

## 使用可选诊断模式

AICPU延迟提交诊断，在`--`前加`--comm-only`：

```bash
bash run_alg_bench.sh -n 2 -p 2 --comm-only -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K
```

该参数等价于`--measurement-mode comm_only`，不能同时指定。工具自动构建匹配模式的包；有效结果的展开和kernel列显示`NA`，通信和带宽仍有实测值。此模式含人工延迟，不代表严格剥离框架开销；CCU不支持。

只验证设备执行和结果正确时，在`--`前加`--no-profiling`。该模式只报告整算子计时，其他阶段列为空，不能用于验收完整性能数据。

查询接口尚不可用时，可在`--`后加`--query-stub basic`。它只模拟查询候选，后续仍执行真实NPU算子；模拟候选不能证明当前拓扑的实际支持能力。切换查询来源后先正常构建。

## 查看性能结果

终端打印本轮`Run directory:`、执行进度和性能表，默认结果目录为`build_out/runs/run-UUID/`。

| 文件 | 用途 |
| --- | --- |
| `report/report.csv` | 中文性能总表，UTF-8带BOM，支持Excel/WPS识别 |
| `report/summary.json` | 查询来源、测量数量及整轮失败信息 |
| `report/diagnostics.json` | 逐项状态和错误详情 |
| `profile_analysis/profiling_audit.json` | 性能数据关联与解析检查结果 |
| `collected/ranks/rank_*/stdout.log`、`stderr.log` | 各Rank的原始运行日志 |

`Cluster PASS`仅表示进程完成；还需确认`measured > 0`、适用指标有数值且默认采集解析成功。`NO MEASUREMENTS`表示没有性能值。`algorithm_unconfirmed`表示已有真实执行，但实际算法缺少确认依据，不等于没有性能数据。未实测或缺失指标显示`-`，解析失败项的指标显示`PARSE_FAIL`。

主要指标均以微秒为时间单位，含义如下：

| 指标 | 含义 |
| --- | --- |
| AICPU头、展开、通信、尾及kernel耗时 | 根据设备采集的阶段边界统计，kernel列为服务端执行区间 |
| CCU通信耗时 | Rank 0正式轮的Wait均值，即调用方等待完成的时间 |
| CCU框架耗时 | 同轮整算子Event减Wait的均值，是Wait外残差，不能严格解释为纯框架成本 |
| CCU kernel耗时 | Rank 0的整算子Event均值；Event是设备计时区间，Write还包含接收结果复制 |
| CCU LoopGroup耗时 | 设备通信指令事件持续时间累计，不是算法起止插针时间；窗口内没有完整事件时按0计入，不证明实际通信为0 |
| 通信带宽 | 按实际测试形状的带宽字节数及通信耗时计算，单位GB/s |
| kernel启动偏差p95/max | 先求每轮最差节点内各Rank开始时刻的极差，再求正式轮的p95和最大值；不表示绝对跨机时钟偏差 |

## 排查常见问题

| 现象 | 处理 |
| --- | --- |
| 查询头文件或接口缺失 | 核对安装SDK的头文件和查询库是否配套，查看后端构建目录的`query_probe.log`；仅更新源码不会更新安装包 |
| makeself未找到 | 核对所选CANN和PATH；非标准位置可设置`MC2_BENCH_MAKESELF`，脚本须保留配套header |
| 算子包或测试程序构建失败 | 按首条错误检查对应`build.log`，核对SDK、Host HCCL和编译依赖 |
| `--skip-build`校验失败 | 去掉该参数正常重建，不必删除历史结果 |
| 查询无候选或算法未命中 | 先用`--query-only`核对拓扑、引擎和候选，不能把跳过作为性能通过 |
| 性能列为空或解析失败 | 检查是否使用`--no-profiling`，再查看解析审计及各Rank采集文件 |
| CCU导出失败 | 核对SDK的msprof；非标准位置在`--`前加`--msprof /实际路径/msprof` |

更多启动参数可执行`bash run_alg_bench.sh --help`查看。
