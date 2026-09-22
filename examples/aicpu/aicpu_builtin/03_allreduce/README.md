# AICPU MC2 AllReduce样例

## 概述

本样例展示如何使用MC2 builtin接口准备AICPU通信资源并启动AICPU KFC Server，再由AICore
kernel通过HCCL高阶API提交AllReduce Client通信任务。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。每个rank输入一段FP32数据。
Host侧通过`Mc2AcquireCcResCtx`获取通信资源上下文，通过`Mc2CcKernelLaunch`在独立任务流上启动
AICPU KFC Server；AICore侧将同一个`ccResCtx`传入`Hccl::InitV2`，通过`Hccl::AllReduce`与
AICPU KFC Server交互，将通信域内所有rank的输入按元素求和后同步到每个rank。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
03_allreduce
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧流程、AICPU资源准备和AICore通信Client实现
```

## 样例描述

### 功能说明

每个rank的输入为`count`个FP32元素。Host侧为AllReduce配置`AICPU_TS`通信引擎、FP32源/目标
数据类型和SUM归约类型，并申请通信资源上下文。随后，AICore `all_reduce_kernel`通过
`Hccl::AllReduce`将各rank输入按元素求和，结果同步写入每个rank的`recvBuf`。

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server on aicpuStream)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AllReduce (submit AICPU Client request)
  -> recvBuf
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内AICPU AllReduce |
| 通信引擎 | `AICPU_TS`（`OpExecuteConfig`取值2） |
| 归约类型 | SUM（`Mc2SetCcReduceType`设置`HCCL_REDUCE_SUM`） |
| AICPU Server启动 | Host侧调用`Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` |
| 通信任务提交 | AICore侧调用`Hccl::AllReduce<true>` |
| AICore Kernel | `all_reduce_kernel<<<1, nullptr, streamAiv>>>` |
| 数据类型 | FP32 |
| 单rank输入/输出规模 | 256个FP32元素 |
| 支持rank数 | 不超过`MC2_MAX_RANK_SIZE`，当前样例中为8 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，创建AICPU与AICore两条任务流，并申请输入和接收Buffer。
3. 通过`Mc2GetCcArgs`创建MC2参数对象，设置`AICPU_TS`通信引擎、FP32源/目标数据类型和
   SUM归约类型。
4. 通过`Mc2AcquireCcResCtx`基于HCCL通信域申请通信资源上下文`ccResCtx`及其大小。
5. 释放MC2参数对象，并通过`Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)`在
   AICPU任务流上启动KFC Server。Server会阻塞等待AICore Client消息，因此不能与AICore
   kernel共用任务流。
6. 通过`all_reduce_kernel<<<1, nullptr, streamAiv>>>`启动AICore kernel。kernel内部调用
   `hccl.InitV2(contextGM, nullptr)`，其中`contextGM`就是Host侧获取的`ccResCtx`。
7. AICore侧调用`Hccl::AllReduce<true>`提交通信Client任务，调用`Wait`和`SyncAll`等待通信
   完成，最后调用`Finalize`通知Server退出。
8. 先同步AICore任务流，再同步AICPU任务流，将`recvBuf`拷贝回Host侧并打印、校验结果。
9. 销毁HCCL通信域、两条任务流和Device侧内存。`ccResCtx`由通信域管理，不需要单独释放。

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

  两卡场景下，rank `d`的输入元素初始化为`d + 1`，AllReduce求和后每个元素为
  `devCount * (devCount + 1) / 2`。运行成功后终端会输出类似以下信息：

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  rankId: 0, recvBuf: [ 3 3 3 ... ]
  rankId: 1, recvBuf: [ 3 3 3 ... ]
  rankId: 0, expected: 3, validation: PASS
  rankId: 1, expected: 3, validation: PASS
  AllReduce sample pass
  ```

  每个rank均完成AllReduce且校验通过，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`同时传给`Mc2CcKernelLaunch`和AICore
  `all_reduce_kernel`，二者必须使用同一个资源上下文。
- AICPU KFC Server会阻塞等待AICore Client消息，`Mc2CcKernelLaunch`与AICore kernel必须
  使用两条不同的任务流，且先同步AICore任务流、再同步AICPU任务流。
- 归约类通信需要在Host侧通过`Mc2SetCcReduceType`设置归约类型，与AICore侧
  `Hccl::AllReduce`传入的`op`保持一致。
- AICore侧必须调用`Finalize`，AICPU KFC Server收到该消息后才会退出。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 当前样例会使用环境中可见的全部NPU设备，设备数量不能超过`MC2_MAX_RANK_SIZE`。
