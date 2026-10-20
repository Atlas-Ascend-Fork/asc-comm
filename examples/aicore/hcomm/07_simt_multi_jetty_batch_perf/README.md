# Hcomm SIMT多Jetty并行提交性能样例

## 概述

本样例展示如何在Ascend C SIMT Kernel中，基于`AscendC::simt::Hcomm`的Batch提交接口，由多个Jetty并行
发布WQE，并由一个消费者统一处理共享CQ、按JFS归属推进各Jetty的完成状态。支持`WriteNbi`、`ReadNbi`和
`WriteWithNotifyNbi`三类接口，测量多Jetty并行下发时每请求的设备侧下发耗时。

本用例依赖Hcomm内部完成管理接口，用于工程验收，不作为用户API调用示例。`MakeBatchHandle`和每次批次
Write/Read/Notify均显式传入group；同一handle使用固定的Group和UB工作区；发布流程包含三个屏障，共享
CQ由一个消费者统一管理。运行前需安装与本仓库源码匹配的run包并重新编译本样例。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR&950DT系列产品 | >= CANN 9.3.0 |

## 目录结构介绍

```text
multi_jetty_batch_perf
├── CMakeLists.txt                 // 编译工程文件
├── main.cpp                       // Host侧通信域创建、内存注册、用例调度与结果校验
├── host_setup.h                   // Jetty通道创建和远端内存索引解析
├── simt_kernel.cpp                // 单一并行VF与warmup/计时/Drain流程
├── simt_submit.h                  // 三屏障发布顺序与每lane SQ维护
├── simt_completion.h              // CompletionSet初始化/接续、窗口登记、完成快照
├── validation.h                   // 完成计数、全部payload字节、Notify值校验
├── multi_jetty_batch_perf_common.h // 共享常量及主机/设备数据布局
├── README.md                      // 中文样例说明
└── README_en.md                   // 英文样例说明
```

## 样例描述

### 功能说明

| API | 数据流向 | 语义说明 |
|------|---------|------|
| `WriteNbi` | 本地GM → 远端GM | 将本地Global Memory数据写入远端NPU指定地址。 |
| `ReadNbi` | 远端GM → 本地GM | 从远端NPU指定地址读取数据到本地Global Memory。 |
| `WriteWithNotifyNbi` | 本地GM → 远端GM | 写入payload后，再写一个远端notify word。 |

MultiJetty提交方式：

- `--jetty-counts`指定该rank对的Jetty总数；每Jetty的lane数等于batch size，每lane构造一个请求，
  由最后一个publisher lane发布整批，因此每批请求数也等于每Jetty的lane数。lane数和每lane请求数
  不能独立配置。
- 每个lane维护自己构造的SQ，多个Jetty的publisher并行发布；三个block屏障协调发布前、发布后和
  下一批开始，组间会在这些边界等待。共享CQ的单消费者规则只约束完成管理，并不要求Jetty轮流提交。
- 单kernel内可并行的Jetty数由编译期batch size决定（`kMaxActiveLanes`/batch size，向下取整）；
  Jetty数超出时在同一条stream中拆成多个kernel接续执行，第二个kernel接续同一CompletionSet，
  不清零CQ游标。输出以`Jettys`和`ConcurrentJettys`区分配置数量与实际最大并行数量。

数据校验策略：

- 每项用例同时要求完成计数PASS（`COMPLETION_CHECK`）和对应端数据校验通过；Notify还要求所有
  通知值通过。Read检查发起端，Write/Notify检查接收端，包含预热数据。
- 发送端和接收端按jetty/operation填充identity pattern，逐字节校验全部payload与notify word。

### 样例规格

| 项目 | 说明 |
|------|------|
| 通信模式 | 两rank点对点通信（Jetty数可配置，多通道并行发布） |
| 调用方式 | SIMT Kernel内`AscendC::simt::Hcomm`接口多Jetty并行下发 |
| 测试接口 | `write`、`read`、`notify`，`all`表示依次执行三类接口 |
| 数据规格 | 固定64B payload、每Jetty 1024计时请求、warmup 96按batch size向上对齐 |
| 进程启动 | 可执行文件直接拉起，内部fork多rank |
| 支持rank数 | 2（固定一对） |

### 实现流程

1. 可执行文件直接拉起2个rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换root info并创建
   通信域。每个用例由父进程独立拉起一轮rank进程。
2. 调用`HcclCommMemReg`向通信域注册本卡send/recv两个通信buffer；通过`HcclRankGraphGetLayers`和
   `HcclRankGraphGetLinks`获取对端链路Endpoint，调用`HcclChannelAcquire`批量创建Jetty数量的
   P2P通道（引擎`COMM_ENGINE_AIV`、协议`COMM_PROTOCOL_UBC_CTP`），并通过`HcclChannelGetRemoteMems`
   解析各通道远端recv buffer的注册索引。
3. Host按jetty/operation预初始化identity pattern，barrier同步后发送方按kernel拆分依次发起Kernel。
4. Kernel侧每个Jetty的group执行`Init()`和`MakeBatchHandle`后，按三屏障顺序发布warmup与计时请求，
   共享CQ由单消费者推进，最后`Drain(CompletionSet)`等待完成；每个Jetty留存完成快照供Host校验。
5. 发送方回读计时与完成状态，校验完成计数；Read接口同时校验本地落地数据；接收方校验Write/Notify
   落地数据与notify值。
6. 两端校验结果经控制通道allgather汇聚，由发送方输出该pair的RESULT行，父进程输出汇总结果。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  样例编译时使用已安装到CANN目录的asc-comm头文件。若尚未安装，先在仓库根目录生成并安装开发验证
  run包，参数说明参见[`docs/zh/compilation/build_and_test.md`](../../../../docs/zh/compilation/build_and_test.md)：

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_9.3.0_linux-<arch>.run --full
  ```

  然后加载CANN环境变量：

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 样例执行

  在本样例目录下执行如下命令。可执行文件内部fork出全部rank进程，直接运行即可。每次只编译一个
  batch size的并行VF，切换batch size需重新cmake配置：

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 -DMULTI_JETTY_MATRIX_BATCH=1 ..
  make -j
  cd .. && ./build/multi_jetty_batch_perf tcp://127.0.0.1:29628 2
  ```

  常用执行方式示例：

  ```bash
  # 默认执行Write/Read/Notify × J1/2/4/8，共12项。
  ./build/multi_jetty_batch_perf tcp://127.0.0.1:29628 2
  # 只执行write接口。
  ./build/multi_jetty_batch_perf tcp://127.0.0.1:29628 2 write
  # 指定Jetty数列表。
  ./build/multi_jetty_batch_perf tcp://127.0.0.1:29628 2 write --jetty-counts 4,8
  # 对8、32、64、128、256分别以-DMULTI_JETTY_MATRIX_BATCH=<N>重新编译后重复上述命令。
  ```

- 启动参数说明

  ```bash
  ./build/multi_jetty_batch_perf <ip:port> <nranks> [write|read|notify|all] [options]
  ```

  | 参数 | 默认值 | 说明 |
  |------|--------|------|
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 拉起的rank进程数，本样例固定为2 |
  | `<api>` | `all` | 测试的通信接口；`all`表示依次执行`write`/`read`/`notify` |
  | `--jetty-counts` | `1,2,4,8` | Jetty数列表（逗号分隔），每个值须为1、2、4、8之一 |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  |------|--------|------|
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510`（默认） | NPU架构：`dav-3510`对应Ascend 950PR&950DT系列产品 |
  | `MULTI_JETTY_MATRIX_BATCH` | `1`/`8`/`32`/`64`/`128`/`256`（默认`256`） | 每批请求数，同时是每Jetty的lane数；每次只编译一个batch size的并行VF，须与运行时的数据几何匹配 |

- 执行结果

  多卡执行成功后，终端将输出如下信息，每个用例一行，随后为汇总结果：

  ```text
  RESULT | API=write | Mode=MultiJetty | DataSize/B=64 | BatchSize=256 | Jettys=8 | ConcurrentJettys=4 | IssueTicksPerRequest=123.456789 | Pair=0-1 | Status=PASS
  RESULT | Example=multi_jetty_batch_perf | Status=PASS
  ```

  主要指标`IssueTicksPerRequest`=提交计时区间的设备`clock()`差值/总请求数，越小越好；原始clock()
  ticks，未换算为ns。包含构造/发布和提交路径同步，不包含随后`Drain`等待；不计预热，多kernel累加
  提交区间后除以总请求数，也不计host launch间隙。只比较PASS行，不与未对齐时钟口径的SIMD数据直接
  计算加速比。正常运行同时输出`COMPLETION_STATE`、`COMPLETION_CHECK`与`DATA_AUDIT_SUMMARY`汇总，
  校验失败时展开明细。

## 注意事项

- 运行样例需要2张NPU；单卡环境仅支持编译验证。
- batch size是编译期常量（`MULTI_JETTY_MATRIX_BATCH`），kernel按该值选择并行VF几何；切换batch
  size须重新cmake配置并编译。
- 编译开启`--cce-res-usage`，构建日志中会输出VF资源报告（stack/寄存器用量），可用于核对资源约束。
- `IssueTicksPerRequest`衡量平均下发成本，不是单个请求的端到端延迟；未对齐时钟口径前，不与归档
  SIMD数据直接计算加速比。
- 未执行的用例组合不会计入PASS：任一用例被中断（Ctrl+C/SIGTERM）时汇总结果为INTERRUPTED。
