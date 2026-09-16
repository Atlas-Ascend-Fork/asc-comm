# Hcomm SIMT URMA 性能样例

## 概述

本样例测量SIMT路径上五个点对点接口的发送性能：`WriteNbi`、`WriteValueNbi`、`WriteWithNotifyNbi`、
`AtomicFAA`和`AtomicCAS`，覆盖立即提交与延迟提交两种发布策略，分别统计WQE下发时延和完成带宽。

这是点对点基准测试：`<nranks>`可以 >= 2，但只有rank 0（发送方）和rank 1（接收方）建链并计时，
其余rank打印`SKIP`后直接退出。只保留一个发送方是有意的——多方并发发送会互相争抢链路和SQ资源，
测出的时延和带宽不再具有可比性。需要多卡收发验证时使用
[`simt_urma`](../04_simt_urma/README.md)功能样例。

所有操作均由单个lane发起，一个channel必须由一个lane驱动。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
simt_urma_perftest
├── CMakeLists.txt               // 编译工程文件
├── main.cpp                     // Host侧通信域、内存注册和计时流程
├── op_kernel.cpp                // SIMT Kernel侧URMA接口调用
├── simt_urma_perftest_common.h  // Host与Kernel共享定义
├── README.md                    // 中文样例说明
└── README_en.md                 // 英文样例说明
```

## 样例描述

### 功能说明

五个接口 × 两种发布策略：

| `api` | 说明 |
| --- | --- |
| `write` | `WriteNbi`，WQE通过SGE指向本地缓冲区，payload长度可变 |
| `write_value` | `WriteValueNbi`，8字节payload内联在WQE中，不读本地缓冲区 |
| `notify` | `WriteWithNotifyNbi`，payload之后再写一个远端signal word，payload长度可变 |
| `faa` | `AtomicFAA`，对同一个远端8字节word累加 |
| `cas` | `AtomicCAS`，对同一个远端8字节word比较交换 |

| `commit-mode` | 说明 |
| --- | --- |
| `immediate` | 每条WQE都`commit=true`，各自敲一次doorbell |
| `last` | 除最后一条外全部`commit=false`，由最后一条提交发布整批 |

两种策略的SQ占用也不同：延迟提交的`WriteNbi`占1个basic block，立即提交的要把payload拆成两个SGE
以填满128字节DWQE窗口，占2个；其余四个接口无论哪种策略都是固定2 BB。日志中的`SqBlocksPerWqe`
给出实际占用。

计时流程为：warmup次接口下发后`Drain`（不计时），随后正式下发`iterations`次并记录`IssueTime`
（只统计正式WQE下发，用于比较接口提交时延），`Drain`结束记录`CompletionTime`（仅用于计算
`CompletionBandwidth`）。`DrainStatus`和`Completed`用于确认本次下发最终正确完成。

连续的WQE轮转多个slot，避免都落在同一条cache line上——否则测到的是远端内存系统而不是下发路径。
两个原子接口是例外：它们必须打在同一个word上，累加才有意义，因此不轮转。

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | 点对点基准测试，仅rank 0发送、rank 1接收 |
| 调用方式 | SIMT Kernel内直接调用URMA接口 |
| 测试接口 | `write`、`write_value`、`notify`、`faa`、`cas` |
| 发布策略 | `immediate`（默认）、`last` |
| 支持rank数 | 任意 >= 2（rank 2及以上不参与通信） |

### 实现流程

1. 可执行文件直接拉起全部rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换root info并
   创建通信域；仅rank 0和rank 1加入通信域，其余rank直接退出。
2. rank 0和rank 1各自注册内存、创建Channel并交换内存地址。
3. rank 0下发warmup次接口后`Drain`，随后计时下发`iterations`次接口，再次`Drain`。
4. rank 0记录`IssueTime`、`CompletionTime`和`Completed`，rank 1校验落地数据。
5. 输出RESULT行。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  样例编译时使用已安装到CANN目录的asc-comm头文件。若尚未安装，先在仓库根目录生成并安装开发验证
  run包，参数说明参见[`docs/zh/guide/build_and_test.md`](../../../../docs/zh/guide/build_and_test.md)：

  ```bash
  bash build.sh --pkg
  ./build_out/cann-asc-comm_1.0.0_linux-<arch>.run --full
  ```

  然后加载CANN环境变量：

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 样例执行

  在本样例目录下执行如下命令。可执行文件内部fork出全部rank进程，直接运行即可：

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/simt_urma_perftest tcp://127.0.0.1:29621 2 write
  ```

- 启动参数说明

  ```bash
  ./build/simt_urma_perftest <ip:port> <nranks> <write|write_value|notify|faa|cas> [options]
  ```

  | 参数 | 默认值 | 说明 |
  | --- | --- | --- |
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 拉起的rank进程数，必须 >= 2（仅rank 0/1参与测试） |
  | `--iterations` | `1024` | 计时的WQE数量 |
  | `--warmup` | `100` | 预热WQE数量 |
  | `--payload-bytes` | `4096` | `write`和`notify`的payload字节数，最小为2；`write_value`恒为8字节，`faa`和`cas`恒为8字节，三者忽略该选项 |
  | `--commit-mode` | `immediate` | `immediate`（每WQE一次doorbell）或`last`（除最后一条外全部延迟提交） |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR/Ascend 950DT |

- 执行结果

  ```text
  RESULT | Path=SIMT-t1 | API=write | DataSize/B=4096 | WqeCount=1024 | Warmup=100 | CommitMode=Immediate | SqBlocksPerWqe=2 | Slots=64 | IssueTime/us=... | AverageIssue/ns=... | CompletionTime/us=... | CompletionBandwidth/GB/s=... | DrainStatus=0 | Completed=1024
  [rank 1] simt_urma_perftest write PASS
  ```

  `DrainStatus=0`、`Completed`与`WqeCount`一致且rank 1输出PASS，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- `--payload-bytes`最小为2：立即提交的`WriteNbi`把payload拆成两段SGE，1字节会让第一段长度为0，
  而URMA不接受长度为0的SGE。
- 接收侧只校验`write`和`write_value`：`notify`的signal word与payload共用同一个slot，两个原子
  接口每次迭代都会改写目标word，落地镜像取决于迭代次数，没有稳定的期望值。这两类用例依赖
  `DrainStatus`和`Completed`判断是否正确完成。
- 需要payload扫描时，在多个`--payload-bytes`取值（如`8 16 64 256 1024 4096 16384 65536`）上
  各运行一次可执行文件即可。
- 编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
