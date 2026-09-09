# CCU Builtin AlltoAll样例

## 概述

本样例展示如何使用MC2 builtin接口准备CCU通信资源并启动CCU Server，再由AICore
kernel通过HCCL高阶API提交AlltoAll Client通信任务。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。每个rank向通信域内
每个目标rank发送256个FP32元素。Host侧通过`Mc2AcquireCcResCtx`获取CCU资源上下文，
通过`Mc2CcKernelLaunch`启动CCU Server；AICore侧将同一个`ccResCtx`传入`Hccl::InitV2`，
通过`Hccl::AlltoAll`完成等长数据交换。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
05_all2all
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧流程、CCU资源准备和AICore通信Client实现
```

## 样例描述

### 功能说明

每个rank的输入按目标rank划分为多个连续数据块，每个数据块包含256个FP32元素。
发送rank `i`发往目标rank `j`的数据块初始化为`i * 1000 + j`。AlltoAll完成后，
接收rank `j`的第`i`个数据块应为`i * 1000 + j`，用于验证数据交换结果。

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AlltoAll<true> (submit CCU Client request)
  -> recvBuf
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU AlltoAll |
| CCU Server启动 | Host侧调用`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| 通信任务提交 | AICore侧调用`Hccl::AlltoAll<true>` |
| AICore Kernel | `all_to_all_kernel<<<1, nullptr, streamAiv>>>` |
| 数据类型 | FP32 |
| 每个目标rank的数据量 | 256个FP32元素 |
| 每个rank输入/输出规模 | `rankSize * 256`个FP32元素 |
| 算法配置 | `CcuSchedAllToAllSoleMesh` |
| 支持rank数 | 不超过`CCU_MAX_RANK_SIZE`，当前样例中为8 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，创建AICore Stream，并申请输入和输出Buffer。
3. 通过`Mc2GetCcArgs`创建MC2参数对象，设置`CCU_SCHED`通信引擎、FP32源/目标数据类型和
   `CcuSchedAllToAllSoleMesh`算法配置。
4. 通过`Mc2AcquireCcResCtx`申请CCU资源上下文`ccResCtx`及其大小。
5. 释放MC2参数对象，并通过`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)`启动CCU Server。
   `stream`参数为AICPU通路预留，当前CCU通路不使用。
6. 通过`all_to_all_kernel<<<1, nullptr, streamAiv>>>`启动AICore kernel。kernel内部调用
   `hccl.InitV2(contextGM, nullptr)`，其中`contextGM`就是Host侧获取的`ccResCtx`。
7. AICore侧调用`Hccl::AlltoAll<true>`提交等长数据交换Client任务，调用`Wait`和`SyncAll`等待完成。
8. 同步AICore Stream，将`recvBuf`拷回Host侧，并按发送源rank和本地rank校验各数据块。
9. 销毁HCCL通信域、Stream和Device侧内存。`ccResCtx`由通信域管理，不需要单独释放。

## 编译运行

在本样例目录下执行以下命令。本样例仅支持NPU运行模式。

```bash
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
./demo
```

其中`${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

两卡场景下，rank `d`向目标rank `j`发送的块中每个元素为`d * 1000 + j`，
运行成功后每个rank的接收块应按源rank顺序排列：

```text
Found 2 NPU device(s) available
rankId: 0, recv blocks: [ 0 x 256] [1000 x 256]
rankId: 0, validation: PASS
rankId: 1, recv blocks: [ 1 x 256] [1001 x 256]
rankId: 1, validation: PASS
```

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`同时传给`Mc2CcKernelLaunch`和AICore
  `all_to_all_kernel`，二者必须使用同一个资源上下文。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 当前样例使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`（8）。
