# CCU Builtin ReduceScatter样例

## 概述

本样例展示如何使用MC2 builtin接口准备CCU通信资源并启动CCU Server，再由AICore
kernel通过HCCL高阶API提交ReduceScatter Client通信任务。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。每个rank输入
`rankSize * 256`个FP32元素，经过SUM规约后，每个rank得到256个元素。Host侧通过
`Mc2AcquireCcResCtx`获取CCU资源上下文，通过`Mc2CcKernelLaunch`启动CCU Server；
AICore侧将同一个`ccResCtx`传入`Hccl::InitV2`，通过`Hccl::ReduceScatter`完成通信。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
04_reducescatter
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧流程、CCU资源准备和AICore通信Client实现
```

## 样例描述

### 功能说明

每个rank的输入包含`rankSize * 256`个FP32元素，所有元素初始化为`rankId + 1`。
ReduceScatter对所有rank的对应数据执行SUM规约，并将结果按rank切分，每个rank得到
256个元素。Host侧配置CCU通信引擎、数据类型、SUM规约类型和兜底算法，AICore侧
通过`Hccl::ReduceScatter<true>`提交通信任务。

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::ReduceScatter<true>(SUM) (submit CCU Client request)
  -> recvBuf
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU ReduceScatter |
| CCU Server启动 | Host侧调用`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| 通信任务提交 | AICore侧调用`Hccl::ReduceScatter<true>` |
| Reduce操作 | `HCCL_REDUCE_SUM` |
| AICore Kernel | `reduce_scatter_kernel<<<1, nullptr, streamAiv>>>` |
| 数据类型 | FP32 |
| 每个rank输入规模 | `rankSize * 256`个FP32元素 |
| 每个rank输出规模 | 256个FP32元素 |
| 算法配置 | `CcuSchedReduceScatterSoleMesh` |
| 支持rank数 | 不超过`CCU_MAX_RANK_SIZE`，当前样例中为8 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，创建AICore Stream，并申请输入和输出Buffer。
3. 通过`Mc2GetCcArgs`创建MC2参数对象，设置`CCU_SCHED`通信引擎、FP32源/目标数据类型、
   `HCCL_REDUCE_SUM`规约类型和`CcuSchedReduceScatterSoleMesh`算法配置。
4. 通过`Mc2AcquireCcResCtx`申请CCU资源上下文`ccResCtx`及其大小。
5. 释放MC2参数对象，并通过`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)`启动CCU Server。
   `stream`参数为AICPU通路预留，当前CCU通路不使用。
6. 通过`reduce_scatter_kernel<<<1, nullptr, streamAiv>>>`启动AICore kernel。kernel内部调用
   `hccl.InitV2(contextGM, nullptr)`，其中`contextGM`就是Host侧获取的`ccResCtx`。
7. AICore侧调用`Hccl::ReduceScatter<true>`提交SUM通信Client任务，调用`Wait`和`SyncAll`等待完成。
8. 同步AICore Stream，将`recvBuf`拷回Host侧，并校验每个元素是否等于
   `1 + 2 + ... + rankSize`。
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

两卡场景下，每个rank的输出元素应为`1 + 2 = 3`：

```text
Found 2 NPU device(s) available
rankId: 0, recvBuf: [ 3 3 3 ... ]
rankId: 0, expected: 3, validation: PASS
rankId: 1, recvBuf: [ 3 3 3 ... ]
rankId: 1, expected: 3, validation: PASS
```

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`同时传给`Mc2CcKernelLaunch`和AICore
  `reduce_scatter_kernel`，二者必须使用同一个资源上下文。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 当前样例使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`（8）。
