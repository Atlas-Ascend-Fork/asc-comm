# SIMT Jetty WRITE样例

## 概述

本样例展示如何在AscendC SIMT Kernel中通过`asccomm_work_queue/asccomm_work_queue_simt.h`接口使用Jetty点对点通信能力。样例只保留warp级别的写调用：`write`模式覆盖连续源内存的普通write接口，`write-sge`模式覆盖多个SGE的write接口。

Kernel侧通过`asccomm_peer`和`asccomm_work_queue`接口直接从GM Jetty表解析队列与远端信息并提交数据面通信任务。两个模式写入的长度均由编译期SGE数量决定，便于直接比较。

两个模式都在单次`write`调用内完成WQE构造、SQ写入与DWQE发布，并在`write`返回后调用warp级`drain`等待完成。

这是点对点验证：`<nranks>`可以 >= 2，但只有rank 0（发送方）和rank 1（接收方）建链并执行，其余rank打印`SKIP`后直接退出，不加入通信域。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.3.0 |

## 目录结构介绍

```text
02_simt_asccomm_work_queue
├── CMakeLists.txt          // 编译工程文件
├── simt_work_queue.asc          // Host侧通信域、内存注册和校验流程
├── op_kernel.cpp           // SIMT Kernel侧Jetty接口调用
├── simt_work_queue_common.h     // Host与Kernel共享定义
├── README.md               // 中文样例说明
└── README_en.md            // 英文样例说明
```

## 样例描述

### 功能说明

样例提供两种warp级测试模式，均可通过`all`（默认）顺序执行：

| 模式 | 说明 |
| --- | --- |
| `write` | warp调用连续源内存的普通write接口 |
| `write-sge` | warp调用多SGE write接口，每个SGE写入一个slot |

**正确性校验与压测**：每一轮rank 0下发一次kernel，接收侧校验全部槽位（写入范围之外的槽位应保持清零）后清零目标缓冲区，再进入下一轮。因此`--iterations N`同时也是同一个channel/SQ上的重复压测轮数，后一轮未实际下发时不会被前一轮结果掩盖。

**性能测试**：`--timing`使用设备侧`clock()`打点。性能循环单独调用`write`组WQE，在`write`返回后记录`write_cycles`，再调用提交并单独记录`commit_cycles`，最后在`drain`返回后记录完整完成耗时`completion_cycles`，因此`average_write_us`不包含提交时间。warmup与全部计时轮在单次kernel内完成，每轮都会`drain`，确保SQ tail持续推进；host launch和同步时间不计入上述设备周期。结果中的周期按当前设备1 GHz时钟换算为微秒，与`simt_urma_perftest`的统计方式一致。

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | 点对点SIMT Jetty通信，仅rank 0发送、rank 1接收 |
| 通信协议 | `COMM_PROTOCOL_UBC_CTP` |
| 调用方式 | SIMT Kernel内直接调用Jetty接口 |
| 测试模式 | `write`、`write-sge`、`all`（默认） |
| 支持rank数 | 任意 >= 2（rank 2及以上不参与通信） |

### 实现流程

1. 可执行文件直接拉起全部rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank 2及以上直接退出，rank 0和rank 1通过命令行指定的TCP控制通道
   （`examples/aicore/utils/rank_sync.h`）交换root info并创建通信域。
2. rank 0和rank 1各自注册`send_buf`和`recv_buf`，创建UBC_CTP Channel，并把channel handle写入
   device侧Jetty表供kernel解析。
3. 正确性模式下逐轮下发kernel，每轮经过`written`/`checked`两个barrier：发送侧等待写入落地，
   接收侧校验并清零后再放行下一轮；计时模式下warmup和全部计时轮在单次kernel内完成。
4. 接收侧校验`recv_buf`各槽位与期望值，输出PASS/FAIL；主进程汇总所有模式的执行结果。

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

  在本样例目录下执行如下命令。可执行文件内部fork出全部rank进程，直接运行即可：

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  ./simt_work_queue tcp://127.0.0.1:29628 2
  ```

  只执行单个模式，避免失败用例污染后续SQ/CQ状态（每个模式使用独立的进程组，互不影响）：

  ```bash
  ./simt_work_queue tcp://127.0.0.1:29628 2 write
  ./simt_work_queue tcp://127.0.0.1:29628 2 write-sge
  ```

  正确性压测与性能测试：

  ```bash
  # 同channel/SQ重复校验100轮
  ./simt_work_queue tcp://127.0.0.1:29628 2 write-sge --iterations 100
  # 设备侧计时：warmup 100轮、计时1000轮
  ./simt_work_queue tcp://127.0.0.1:29628 2 write --timing --warmup 100 --iterations 1000
  ```

- 启动参数说明

  ```bash
  ./simt_work_queue <tcp://ip:port> <nranks> [all|write|write-sge] [options]
  ```

  | 参数 | 默认值 | 说明 |
  | --- | --- | --- |
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 拉起的rank进程数，必须为不小于2的整数（仅rank 0/1参与通信） |
  | `[mode]` | `all` | 测试模式，取值见样例描述；`all`顺序执行两个write模式；无法识别的mode会直接报错，不会静默回退 |
  | `--iterations` | `1` | 每个模式的正确性校验轮数 |
  | `--warmup` | `0` | 计时模式的预热轮数，必须与`--timing`同时使用 |
  | `--timing` | 关闭 | 设备侧计时模式：全部轮次在单次kernel内完成并输出各阶段周期 |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR&950DT系列产品 |
  | `SIMT_WORK_QUEUE_SGE_NUM` | `12` | `write-sge`模式的编译期SGE数量；`write`模式按相同数量传输连续源内存，范围1到12 |
  | `HCOMM_CCE_RES_USAGE` | `OFF` | 开启后在编译阶段打印SIMT VF的栈和寄存器占用，用于无硬件时检查资源占用 |

- 执行结果

  以2卡运行`write`模式为例：

  ```text
  [rank 1] simt_work_queue write iterations 1 PASS
  RESULT | Example=simt_work_queue Mode=write | Status=PASS
  ```

  计时模式额外输出各阶段设备周期：

  ```text
  PERF | mode=write | iterations=1000 | warmup=100 | write_cycles=... | average_write_cycles=... | average_write_us=... | commit_cycles=... | average_commit_cycles=... | average_commit_us=... | completion_cycles=... | average_completion_cycles=... | average_completion_us=... | completed=1000
  RESULT | Example=simt_work_queue Mode=write | Status=PASS
  ```

  所有请求的模式均输出`PASS`且最终输出`Status=PASS`，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 样例只实际使用rank 0和rank 1，其余rank打印`SKIP`后直接退出，不加入通信域。
- 样例仅支持单机多卡运行，不支持跨节点多机运行。
- Kernel侧通过`AscendC::simt::LocalBufferAddr`解析缓冲区基址时，传入的是通道缓冲区表的下标，而不是
  `memHandles`数组下标：本地表下标0由CCL buffer预留，第一个显式注册的缓冲区使用下标1；远端缓冲区
  下标通过`HcclChannelGetRemoteMems`按tag查找。
- `--warmup`必须与`--timing`同时使用。
- 编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `asccomm`库。
