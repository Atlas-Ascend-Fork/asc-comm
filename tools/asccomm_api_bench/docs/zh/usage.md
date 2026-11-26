# AscComm API性能测试工具使用指南

简体中文 | [English](../en/usage.md)

本指南介绍内置后端的日常使用。算子和引擎范围见《[工具说明](../../README.md#支持范围)》，两机执行见《[跨机使用指南](./multinode.md)》。

## 配置环境

需要匹配的CANN开发包、Host HCCL（主机侧集合通信库）、MPICH兼容的MPI多进程环境，以及Python 3.9+、CMake 3.19+和C++11编译器。各设备须空闲，当前用户须有算子包安装目录的读写权限。

CANN开发包须包含正式查询接口`Mc2GetSupportAlgList`、`Mc2SetCcDataLenRange`及匹配的`libmc2_client.so`。默认profiling（设备性能数据采集）需要SDK提供ACL采集接口和`libmsprofiler.so`；CCU原始数据导出还需要匹配SDK的`msprof`。AICPU直接分析原始采集数据，无需导出时间轴。

在Bash中设置安装路径，并进入工具目录。将下面的CANN、MPI和源码路径替换为实际位置；已有这些依赖时不需要重新安装：

```bash
export ASCEND_HOME_PATH=/usr/local/Ascend/cann
export MPI_HOME=/usr/local/mpi/mpich
cd /path/to/asc-comm/tools/asccomm_api_bench
```

工具在构建和设备进程启动时加载所选CANN的环境，并补充MPI的可执行文件和动态库搜索路径。变量名区分大小写；已有正确的环境变量时，可直接使用。

Host HCCL是主机侧集合通信库，默认使用所选CANN目录中的头文件和动态库。仅在Host HCCL独立安装时，额外设置其安装根目录：

```bash
export HCCL_DIR=/实际/HostHCCL安装目录
```

构建优先使用CANN自带的makeself打包工具，再查系统PATH，通常无需单独配置。标准布局下，CCU所需的`libhcomm.so`会自动链接。

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
| `--`前 | `--output-dir` | 新结果目录，省略时自动创建，不能覆盖已有作业 |
| `--`后 | `--op` | 算子名称，必须指定 |
| `--`后 | `-a` | 引擎：`aicpu_ts`或`ccu_sched`，默认`aicpu_ts` |
| `--`后 | `-d` | 数据类型，内置实测使用`fp32`（32位浮点），默认`fp32` |
| `--`后 | `-o` | 归约类型：`sum`、`prod`、`max`、`min`，默认`sum`；实际支持范围由SDK决定 |
| `--`后 | `-b`、`-e` | 起始和目标结束字节数，默认32KiB和256MiB |
| `--`后 | `-f`或`-i` | 倍增因子或线性字节步长，二者互斥，默认`-f 2` |
| `--`后 | `--alg` | 查询返回的完整算法名，省略时测试全部候选，可重复指定 |

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

仅查询仍需要真实设备和通信域，但不构建算子包、不预热、不计时，也不生成性能CSV。结果打印到终端并写入本轮`query_summary.json`。

性能测试会重新查询，命中才进入实测；未命中提示不支持并跳过，不在工具侧另行兜底。范围查询得到的候选并集在完整测量长度序列上测试，不能把查询返回理解为每个长度都保证执行成功。

需要指定算法时，从相同算子和引擎的查询结果复制完整名称。例如，仅当上面的AICPU查询返回`sole[mesh]`时，才使用：

```bash
bash run_alg_bench.sh -n 2 -p 2 -- \
  --op allreduce -a aicpu_ts -b 32K -e 32K --alg 'sole[mesh]'
```

## 执行单机测试

下面命令均使用两张设备和32KiB单点，不指定算法，由工具自动查询并测试该条件下的候选。无需先执行仅查询命令。

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

重复运行会创建新的结果目录，无需删除之前的记录。

## 查看性能结果

终端打印本轮`Run directory:`、执行进度和性能表，默认结果目录为`build_out/runs/run-UUID/`。

CCU性能测试额外打印`Benchmark phase:`阶段进度和`Benchmark exit:`退出信息。是否成功以`status`退出码及报告结果为准，`phase=finished`只表示流程已收尾。

| 文件 | 用途 |
| --- | --- |
| `report/report.csv` | 中文性能总表，UTF-8带BOM，支持Excel/WPS识别 |
| `report/summary.json` | 查询来源、测量数量及整轮失败信息 |
| `report/diagnostics.json` | 逐项状态和错误详情 |
| `profile_analysis/profiling_audit.json` | 性能数据关联与解析检查结果 |
| `collected/ranks/rank_*/stdout.log`、`stderr.log` | 各Rank的原始运行日志 |

`Cluster PASS`仅表示进程完成；还需确认`measured > 0`、适用指标有数值且默认采集解析成功。`NO MEASUREMENTS`表示没有性能值。`algorithm_unconfirmed`表示已有真实执行，但实际算法缺少确认依据，不等于没有性能数据。未实测或缺失指标显示`-`，不适用指标显示`NA`，解析失败项的指标显示`PARSE_FAIL`。

主要指标均以微秒为时间单位，含义如下：

| 指标 | 含义 |
| --- | --- |
| AICPU头、通信、尾耗时 | 根据设备采集的阶段边界统计 |
| CCU通信耗时 | Rank 0正式轮的Wait均值，即调用方等待完成的时间 |
| CCU框架耗时 | 同轮整算子Event减Wait的均值，是Wait外残差，不能严格解释为纯框架成本 |
| CCU LoopGroup耗时 | 设备通信指令事件持续时间累计；窗口内没有完整事件时按0计入，不证明实际通信为0 |
| 通信带宽 | 按实际测试形状的带宽字节数及通信耗时计算，单位GB/s |
| kernel启动偏差p95/max | 先求每轮最差节点内各Rank开始时刻的极差，再求正式轮的p95和最大值；不表示绝对跨机时钟偏差 |

CSV中AICPU为13列，CCU为16列；CSV和终端性能表均不展示“状态”列。逐项状态保存在`report/diagnostics.json`，终端继续打印整轮结果和错误原因。完整终端性能表额外展示执行引擎，不应按终端表列数判断CSV列数。CCU包含算法与KFC阶段耗时，各阶段的均值使用同一批完整样本。整算子Event计时保存在JSON诊断中，不单独作为CCU性能列展示。采样完整性记录在`profile_analysis/profiling_audit.json`中，不作为CSV列展示。没有完整样本时相关计时显示`-`，本轮报告失败，已取得的常规性能值仍保留。

## 排查常见问题

| 现象 | 处理 |
| --- | --- |
| 查询头文件或接口缺失 | 核对安装SDK的头文件和查询库是否配套，查看后端构建目录的`query_probe.log`；仅更新源码不会更新安装包 |
| makeself未找到 | 核对所选CANN的安装是否完整，以及PATH是否正确 |
| 算子包或测试程序构建失败 | 按首条错误检查对应`build.log`，核对SDK、Host HCCL和编译依赖 |
| 查询无候选或算法未命中 | 先用`--query-only`核对拓扑、引擎和候选，不能把跳过作为性能通过 |
| 性能列为空或解析失败 | 查看解析审计及各Rank采集文件，核对采集数据是否完整 |
| CCU导出失败 | 核对SDK的msprof及其依赖是否配套 |

命令参数可执行`bash run_alg_bench.sh --help`查看。
