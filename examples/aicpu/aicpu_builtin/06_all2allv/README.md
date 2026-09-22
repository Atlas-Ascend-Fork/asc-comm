# AICPU MC2 AlltoAllV样例

## 概述

本样例展示如何使用MC2 builtin接口准备AICPU通信资源并启动AICPU KFC Server，再由AICore
kernel通过HCCL高阶API提交变长AlltoAllV Client通信任务。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。与等长AlltoAll不同，
AlltoAllV中每对rank之间的收发数据量按公式定制、互不相同。Host侧通过`Mc2AcquireCcResCtx`
获取通信资源上下文，通过`Mc2CcKernelLaunch`在独立任务流上启动AICPU KFC Server；AICore侧
将同一个`ccResCtx`传入`Hccl::InitV2`，在kernel内按公式推导各peer的收发数量和偏移后，通过
`Hccl::AlltoAllV`完成变长数据交换。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
06_all2allv
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧流程、AICPU资源准备和AICore变长通信Client实现
```

## 样例描述

### 功能说明

收发数据量按如下公式定制，Device侧kernel与Host侧真值计算共用同一公式：

```text
rank i 发往 rank j 的数据量 = BASE_COUNT * (1 + ((2 * i + j) % rankSize))
rank i 从 rank j 接收的数据量 = BASE_COUNT * (1 + ((i + 2 * j) % rankSize))
```

其中`BASE_COUNT`为8。每个rank的发送缓冲区第`idx`个元素填充为`device * 1000 + idx`。
AICore kernel通过`GetRankId`/`GetRankDim`获取本卡rank信息，在kernel内计算各peer的
`sendCounts`/`sdispls`/`recvCounts`/`rdispls`数组后调用`Hccl::AlltoAllV`。

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start AICPU KFC Server on aicpuStream)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> GetRankId/GetRankDim -> 推导收发数量与偏移
  -> Hccl::AlltoAllV (submit AICPU Client request)
  -> recvBuf
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内AICPU AlltoAllV（变长） |
| 通信引擎 | `AICPU_TS`（`OpExecuteConfig`取值2） |
| AICPU Server启动 | Host侧调用`Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)` |
| 通信任务提交 | AICore侧调用`Hccl::AlltoAllV<true>` |
| AICore Kernel | `all_to_all_v_kernel<<<1, nullptr, streamAiv>>>` |
| 数据类型 | FP32 |
| 数据量基准 | `BASE_COUNT = 8`个FP32元素 |
| 单rank收发规模 | 按公式推导，各rank不相同 |
| 支持rank数 | 不超过`MC2_MAX_RANK_SIZE`，当前样例中为8 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，创建AICPU与AICore两条任务流，按公式计算本卡收发元素总数并
   申请发送和接收Buffer。
3. 通过`Mc2GetCcArgs`创建MC2参数对象，设置`AICPU_TS`通信引擎和FP32源/目标数据类型。
4. 通过`Mc2AcquireCcResCtx`基于HCCL通信域按`HCCL_CMD_ALLTOALLV`申请通信资源上下文
   `ccResCtx`及其大小。
5. 释放MC2参数对象，并通过`Mc2CcKernelLaunch(aicpuStream, ccResCtx, ccResCtxSize)`在
   AICPU任务流上启动KFC Server。
6. 通过`all_to_all_v_kernel<<<1, nullptr, streamAiv>>>`启动AICore kernel。kernel内部调用
   `hccl.InitV2(contextGM, nullptr)`后，用`GetRankId`/`GetRankDim`按公式填充收发数量与
   偏移数组，再调用`Hccl::AlltoAllV<true>`提交通信任务。
7. AICore侧调用`Wait`和`SyncAll`等待通信完成，最后调用`Finalize`通知Server退出。
8. 先同步AICore任务流，再同步AICPU任务流，将`recvBuf`拷贝回Host侧，按同一公式构造真值
   并逐元素校验、打印结果。
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

  两卡场景下，rank 0发送`8 * (1 + (0 % 2)) = 8`个元素给rank 0、`8 * (1 + (1 % 2)) = 16`个
  元素给rank 1，发送总量为24；rank 0从rank 0接收8个元素、从rank 1接收
  `8 * (1 + (2 % 2)) = 8`个元素，接收总量为16。运行成功后终端会输出类似以下信息：

  ```text
  Found 2 NPU device(s) available
  rankId: 0, sendCount: 24, recvCount: 16
  rankId: 1, sendCount: 24, recvCount: 32
  rankId: 0, recvBuf head: [ 0 1 2 ... 7 1000 1001 ... ], validation: PASS
  rankId: 1, recvBuf head: [ 8 9 10 ... 23 ], validation: PASS
  AlltoAllV sample pass
  ```

  每个rank均完成AlltoAllV且校验通过，表示样例执行成功。`recvBuf head`仅打印接收结果的前16个
  元素，rank 1的前16个元素全部来自rank 0。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- Device侧kernel与Host侧真值计算必须使用完全一致的收发数量公式，否则校验失败。
- 收发数量数组长度受`MC2_MAX_RANK_SIZE`限制，设备数量不能超过该值。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`同时传给`Mc2CcKernelLaunch`和AICore
  `all_to_all_v_kernel`，二者必须使用同一个资源上下文。
- AICPU KFC Server会阻塞等待AICore Client消息，`Mc2CcKernelLaunch`与AICore kernel必须
  使用两条不同的任务流，且先同步AICore任务流、再同步AICPU任务流。
- AICore侧必须调用`Finalize`，AICPU KFC Server收到该消息后才会退出。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
