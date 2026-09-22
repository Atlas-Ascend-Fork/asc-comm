# CCU Builtin AlltoAllV样例

## 概述

本样例展示如何使用MC2 builtin接口准备CCU通信资源并启动CCU Server，再由AICore
kernel通过HCCL高阶API提交AlltoAllV Client通信任务。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。每个rank向通信域内
每个目标rank发送不同数量的FP32元素。Host侧通过`Mc2AcquireCcResCtx`获取CCU资源上下文，
通过`Mc2CcKernelLaunch`启动CCU Server；AICore侧将同一个`ccResCtx`传入`Hccl::InitV2`，
通过`Hccl::AlltoAllV`完成可变长度数据交换。

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
└── main.asc          // Host侧流程、CCU资源准备和AICore通信Client实现
```

## 样例描述

### 功能说明

每个rank向目标rank `j`发送的元素数量为`256 + 本rank + j`，发送数据在输入Buffer
中按目标rank顺序连续排列。发送rank `i`发往目标rank `j`的数据块初始化为
`i * CCU_MAX_RANK_SIZE + j`。AlltoAllV完成后，接收rank `j`的第`i`个数据块应为相同的小整数值，
用于验证可变长度数据交换结果。该取值方式最大为`CCU_MAX_RANK_SIZE * CCU_MAX_RANK_SIZE - 1`，
切换为`int8_t`或`uint8_t`时不会发生数据截断。

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AlltoAllV<true> (submit CCU Client request)
  -> recvBuf
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU AlltoAllV |
| CCU Server启动 | Host侧调用`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| 通信任务提交 | AICore侧调用`Hccl::AlltoAllV<true>` |
| AICore Kernel | `all_to_all_v_kernel<<<1, nullptr, streamAiv>>>` |
| 数据类型 | FP32 |
| 每个目标rank的数据量 | `256 + 本rank + 目标rank`个FP32元素 |
| 输入/输出规模 | 按各目标rank数据量累加 |
| 算法配置 | `CcuSchedAllToAllVSoleMesh` |
| 支持rank数 | 不超过`CCU_MAX_RANK_SIZE`，当前样例中为8 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，创建AICore Stream，并根据各peer的数据量申请输入和输出Buffer。
3. 通过`Mc2GetCcArgs`创建MC2参数对象，设置`CCU_SCHED`通信引擎、FP32源/目标数据类型和
   `CcuSchedAllToAllVSoleMesh`算法配置。
4. 通过`Mc2AcquireCcResCtx`申请CCU资源上下文`ccResCtx`及其大小。
5. 释放MC2参数对象，并通过`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)`启动CCU Server。
   `stream`参数为AICPU通路预留，当前CCU通路不使用。
6. 通过`all_to_all_v_kernel<<<1, nullptr, streamAiv>>>`启动AICore kernel。kernel内部调用
   `hccl.InitV2(contextGM, nullptr)`，其中`contextGM`就是Host侧获取的`ccResCtx`。
7. AICore侧按rank生成`sendCounts/recvCounts`和`sdispls/rdispls`数组，调用
   `Hccl::AlltoAllV<true>`提交流程，调用`Wait`和`SyncAll`等待完成。
8. 同步AICore Stream，将`recvBuf`拷回Host侧，并按源rank校验每个可变长度数据块。
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

两卡场景下，rank `d`发往目标rank `j`的数据块长度为`256 + d + j`，每个元素值为
`d * CCU_MAX_RANK_SIZE + j`。运行成功后每个rank会打印接收块摘要和`validation: PASS`。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`同时传给`Mc2CcKernelLaunch`和AICore
  `all_to_all_v_kernel`，二者必须使用同一个资源上下文。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 当前样例使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`（8）。
- AlltoAllV的counts和displacements单位都是元素个数，不是字节数。
