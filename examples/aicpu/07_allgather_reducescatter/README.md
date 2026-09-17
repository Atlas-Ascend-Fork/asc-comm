# AICPU MC2 AllGatherReduceScatter样例

## 概述

本样例展示如何使用MC2 builtin接口准备AICPU通信资源并启动AICPU KFC Server，再由AICore
kernel通过HCCL高阶API依次提交AllGather和ReduceScatter两个Client通信任务，构成两级复合通信流水。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。每个rank输入一段FP32数据。
AllGather与ReduceScatter分别为独立的核函数：单次申请的通信资源上下文（`ccResCtx`）仅服务于
一种通信命令，两级流水各自持有独立的上下文，并各自启动一个AICPU KFC Server。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
07_allgather_reducescatter
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧流程、AICPU资源准备和AICore两级通信Client实现
```

## 样例描述

### 功能说明

每个rank的输入为`SEND_COUNT`个FP32元素。阶段一AllGather将各rank输入按rank id顺序聚合到
`gatherBuf`（`SEND_COUNT * rankSize`个元素）；阶段二ReduceScatter将各rank的`gatherBuf`
按元素求和后按rank id切分，每个rank收到第rank id块`SEND_COUNT`个元素。AllGather输出规模
恰好等于ReduceScatter输入规模，`gatherBuf`直接作为阶段二的输入，无需补齐或切分。

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc* (HCCL_CMD_ALLGATHER)
  -> Mc2AcquireCcResCtx -> agCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server #1 on aicpuStream)
  -> Mc2GetCcArgs/Mc2SetCc* (HCCL_CMD_REDUCE_SCATTER)
  -> Mc2AcquireCcResCtx -> rsCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server #2 on aicpuStream)

AICore:
agCtx -> Hccl::InitV2 -> Hccl::AllGather -> gatherBuf
rsCtx -> Hccl::InitV2 -> Hccl::ReduceScatter -> recvBuf
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内AICPU AllGather + ReduceScatter两级复合 |
| 通信引擎 | `AICPU_TS`（`OpExecuteConfig`取值2） |
| 归约类型 | SUM（阶段二`Mc2SetCcReduceType`设置`HCCL_REDUCE_SUM`） |
| AICPU Server启动 | 每阶段各调用一次`Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` |
| 通信任务提交 | AICore侧依次调用`Hccl::AllGather<true>`和`Hccl::ReduceScatter<true>` |
| AICore Kernel | `all_gather_stage_kernel`和`reduce_scatter_stage_kernel`，均为`<<<1, nullptr, streamAiv>>>` |
| 数据类型 | FP32 |
| 单rank输入规模 | 256个FP32元素 |
| AllGather输出/ReduceScatter输入规模 | `rankSize * 256`个FP32元素 |
| 单rank最终输出规模 | 256个FP32元素 |
| 支持rank数 | 不超过`MC2_MAX_RANK_SIZE`，当前样例中为8 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，创建AICPU与AICore两条任务流，并申请输入、聚合和输出Buffer。
3. 阶段一：通过`Mc2GetCcArgs`创建MC2参数对象，设置`AICPU_TS`通信引擎和FP32源/目标数据
   类型，按`HCCL_CMD_ALLGATHER`调用`Mc2AcquireCcResCtx`申请`agCtx`，释放参数对象后通过
   `Mc2CcKernelLaunch`启动第一个KFC Server，再下发`all_gather_stage_kernel`。
4. 先同步AICore任务流，再同步AICPU任务流，等待阶段一完成、第一个Server退出。
5. 阶段二：重新创建MC2参数对象并追加SUM归约类型，按`HCCL_CMD_REDUCE_SCATTER`申请
   `rsCtx`，启动第二个KFC Server，下发`reduce_scatter_stage_kernel`，输入为阶段一的
   `gatherBuf`。
6. 再次先同步AICore任务流、后同步AICPU任务流，等待阶段二完成。
7. 将`gatherBuf`和`recvBuf`拷贝回Host侧，逐级打印并校验结果。
8. 销毁HCCL通信域、两条任务流和Device侧内存。两个`ccResCtx`均由通信域管理，不需要
   单独释放。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  请根据当前环境上CANN开发套件包的安装方式配置开发环境。样例通过
  `Mc2SetCcCommEngine`设置`AICPU_TS`。

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 样例执行

  在本样例目录下执行如下命令。

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  ./demo
  ```

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR/Ascend 950DT |

- 执行结果

  两卡场景下，rank `d`的输入元素初始化为`d + 1`。阶段一AllGather输出为
  `[1 x 256, 2 x 256]`；阶段二各rank的ReduceScatter输入均为AllGather输出且内容一致，
  求和后rank `d`收到`2 * (d + 1)`。运行成功后终端会输出类似以下信息：

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  rankId: 0, allgather output: [ 1 1 1 ... 2 2 2 ... ]
  rankId: 0, reducescatter output: [ 2 2 2 ... ]
  rankId: 0, allgather stage validation: PASS
  rankId: 0, expected: 2, reducescatter stage validation: PASS
  rankId: 1, allgather stage validation: PASS
  rankId: 1, expected: 4, reducescatter stage validation: PASS
  AllGatherReduceScatter sample pass
  ```

  每个rank两级通信均校验通过，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- AllGather与ReduceScatter不能共用同一`ccResCtx`，每个阶段必须按各自的通信命令独立调用
  `Mc2AcquireCcResCtx`申请资源，并各自启动一个AICPU KFC Server。
- 两个阶段的AICore kernel在同一条AICore任务流上顺序执行，由流序保证`gatherBuf`的数据
  依赖；每个阶段都必须先同步AICore任务流、再同步AICPU任务流。
- AICPU KFC Server会阻塞等待AICore Client消息，`Mc2CcKernelLaunch`与AICore kernel必须
  使用两条不同的任务流。
- AICore侧每个阶段都必须调用`Finalize`，对应的AICPU KFC Server收到该消息后才会退出。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 当前样例会使用环境中可见的全部NPU设备，设备数量不能超过`MC2_MAX_RANK_SIZE`。
