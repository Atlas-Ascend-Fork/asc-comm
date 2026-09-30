# AICPU MC2 AllGatherAdd样例

## 概述

本样例展示如何使用MC2 builtin接口准备AICPU通信资源并启动AICPU KFC Server，再由AICore
kernel先执行Add计算、后通过HCCL高阶API提交AllGather Client通信任务，实现计算与通信流水重叠。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。每个rank输入两路FP32数据
`x1`和`x2`。Host侧通过`Mc2AcquireCcResCtx`获取通信资源上下文，获取通信域unfold线程绑定的
展开流，并通过`Mc2CcKernelLaunch`在展开流上启动AICPU KFC Server；AICore侧将同一个`ccResCtx`
传入`Hccl::InitV2`，先以`Hccl::AllGather<false>`仅Prepare通信任务，再执行`tmp = x1 + x2`计算，
计算完成后`Commit`触发通信，使服务端任务准备过程与Add计算重叠。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
02_allgather_add
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧流程、AICPU资源准备和AICore通算融合Client实现
```

## 样例描述

### 功能说明

每个rank的输入为`sendCount`个FP32元素的两路数据`x1`和`x2`。AICore kernel先通过
`AllGather<false>`把通信任务信息发给服务端而不触发执行，随后计算`tmp = x1 + x2`填充发送
缓冲区，再`Commit`通知服务端搬数，最终每个rank的`recvBuf`按rank id顺序聚合所有rank的`tmp`。

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> GetUnfoldThread/AcquireUnfoldStream (获取通信域unfold线程及其展开流)
  -> Mc2CcKernelLaunch (start AICPU KFC Server on unfoldStream)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AllGather<false> (Prepare, 不触发执行)
  -> AddCompute (tmp = x1 + x2, 与服务端任务准备重叠)
  -> Hccl::Commit (触发AllGather)
  -> recvBuf
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内AICPU AllGather，通算融合 |
| 通信引擎 | `AICPU_TS`（`OpExecuteConfig`取值2） |
| AICPU Server启动 | Host侧调用`Mc2CcKernelLaunch(unfoldStream, ccResCtx, ccResCtxSize)`，展开流来自通信域unfold线程 |
| 通信任务提交 | AICore侧调用`Hccl::AllGather<false>` + `Commit` |
| AICore Kernel | `all_gather_add_kernel<<<1, nullptr, streamAiv>>>` |
| 计算逻辑 | `tmp = x1 + x2`，分tile执行，tile长度256 |
| 数据类型 | FP32 |
| 单rank输入规模 | `x1`、`x2`各256个FP32元素 |
| AllGather输出规模 | `rankSize * 256`个FP32元素 |
| 支持rank数 | 不超过`MC2_MAX_RANK_SIZE`，当前样例中为8 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，创建AICore任务流`streamAiv`，并申请`x1`、`x2`、`tmp`和接收Buffer。
   AICPU KFC Server使用通信域unfold线程绑定的展开流，无需自建。
3. 通过`Mc2GetCcArgs`创建MC2参数对象，设置`AICPU_TS`通信引擎和FP32源/目标数据类型。
4. 通过`Mc2AcquireCcResCtx`基于HCCL通信域申请通信资源上下文`ccResCtx`及其大小。
5. 释放MC2参数对象，通过`GetUnfoldThread`获取（或复用）以`"%s_unfold"`为tag持久化在
   `COMM_ENGINE_CPU_TS`引擎上下文中的unfold线程，再通过`AcquireUnfoldStream`取得其
   绑定的展开流，最后通过`Mc2CcKernelLaunch(unfoldStream, ccResCtx, ccResCtxSize)`在展开流上
   启动KFC Server。
6. 通过`all_gather_add_kernel<<<1, nullptr, streamAiv>>>`启动AICore kernel。kernel内部调用
   `hccl.InitV2(contextGM, nullptr)`，其中`contextGM`就是Host侧获取的`ccResCtx`。
7. AICore侧调用`Hccl::AllGather<false>`仅Prepare通信任务，执行`AddCompute`计算
   `tmp = x1 + x2`，再调用`Commit`通知服务端执行通信，`Wait`和`SyncAll`等待通信完成，
   最后调用`Finalize`通知Server退出。
8. 先同步AICore任务流`streamAiv`，再同步展开流`unfoldStream`，将`recvBuf`拷贝回Host侧并
   打印、校验结果。
9. 销毁HCCL通信域、AICore任务流和Device侧内存。`ccResCtx`由通信域管理，不需要单独释放；
   展开流由通信域创建并管理，随通信域销毁释放，调用者不可销毁。

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

  两卡场景下，rank `d`的`x1`和`x2`输入元素均初始化为`d + 1`，`tmp = x1 + x2`为`2 * (d + 1)`。
  运行成功后终端会输出类似以下信息：

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input x1/x2: [ 1 1 1 ... ]
  rankId: 1, input x1/x2: [ 2 2 2 ... ]
  rankId: 0, recvBuf: [ 2 2 2 ... 4 4 4 ... ]
  rankId: 1, recvBuf: [ 2 2 2 ... 4 4 4 ... ]
  rankId: 0, validation: PASS
  rankId: 1, validation: PASS
  AllGatherAdd sample pass
  ```

  每个rank均完成Add计算与AllGather且校验通过，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`同时传给`Mc2CcKernelLaunch`和AICore
  `all_gather_add_kernel`，二者必须使用同一个资源上下文。
- AICPU KFC Server会阻塞等待AICore Client消息，`Mc2CcKernelLaunch`与AICore kernel必须
  使用两条不同的任务流，且先同步AICore任务流、再同步展开流。
- 展开流由HCCL通信域创建并管理，随通信域销毁释放，调用者不可销毁；unfold线程以
  `"%s_unfold"`为tag持久化在`COMM_ENGINE_CPU_TS`引擎上下文中，已存在时直接复用。
- KFC Server在通信域unfold线程的展开流上下发，而不是自建任务流：与HCCL自身通信任务混跑时，
  复用展开流可以避免下发通路冲突。
- `AllGather<false>`仅Prepare不触发执行，必须在发送缓冲区`tmp`计算完成后再调用`Commit`，
  否则服务端可能搬运到未就绪的数据。
- AICore侧必须调用`Finalize`，AICPU KFC Server收到该消息后才会退出。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 当前样例会使用环境中可见的全部NPU设备，设备数量不能超过`MC2_MAX_RANK_SIZE`。
