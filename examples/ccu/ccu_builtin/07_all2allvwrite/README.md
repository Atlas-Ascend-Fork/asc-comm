# CCU Builtin AlltoAllvWrite样例

## 概述

本样例展示如何使用MC2 builtin接口准备CCU通信资源并启动CCU Server，再由AICore
kernel通过HCCL高阶API提交AlltoAllvWrite Client通信任务。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。每个rank向通信域内
每个目标rank发送256个FP32元素。AlltoAllvWrite按字节描述发送偏移和发送大小，并将数据
写入目标rank的CCU scratch window。Host侧通过`Mc2AcquireCcResCtx`获取资源上下文，
通过`Mc2CcKernelLaunch`启动CCU Server；AICore侧将同一个`ccResCtx`传入`Hccl::InitV2`，
通过`Hccl::AlltoAllvWrite`提交通信任务。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
07_all2allvwrite
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧流程、CCU资源准备和AICore通信Client实现
```

## 样例描述

### 功能说明

每个rank向目标rank `j`发送256个FP32元素，输入Buffer按目标rank顺序连续排列。
发送rank `i`发往目标rank `j`的数据块使用固定的小整数`i * CCU_MAX_RANK_SIZE + j`初始化。
AlltoAllvWrite完成后，接收rank `j`的CCU scratch window中，按源rank顺序排列的数据块
应为对应源/目标rank的小整数值。该取值方式最大为`CCU_MAX_RANK_SIZE * CCU_MAX_RANK_SIZE - 1`，
切换为`int8_t`或`uint8_t`时不会发生数据截断。

AlltoAllvWrite的偏移和大小参数单位为字节。由于950 CCU算法按双 die处理，样例按照
接口约定为每个目标rank构造长度为`2 * rankSize`的`sendSizes/sendOffsets`数组，将每个
目标rank的数据切分为两个 die 分片。

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc*
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AlltoAllvWrite<true> (submit CCU Client request)
  -> CCU scratch window
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU AlltoAllvWrite |
| CCU Server启动 | Host侧调用`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| 通信任务提交 | AICore侧调用`Hccl::AlltoAllvWrite<true>` |
| AICore Kernel | `all_to_all_v_write_kernel<<<1, nullptr, streamAiv>>>` |
| 数据类型 | FP32 payload，通信元数据按INT8字节处理 |
| 每个目标rank的数据量 | 256个FP32元素，即1024字节 |
| 输出位置 | `ccResCtx`中的`res[0]`指向的CCU scratch window |
| 算法配置 | `CcuHalfAll2AllVMesh1D` |
| 支持rank数 | 不超过`CCU_MAX_RANK_SIZE`，当前样例中为8 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，创建AICore Stream，申请输入Buffer和保存
   `sendSizes/sendOffsets`的GM Buffer。
3. 初始化每个目标rank对应的发送数据，并按950 CCU双 die约定填充发送大小和字节偏移。
4. 通过`Mc2GetCcArgs`创建MC2参数对象，设置`CCU_SCHED`通信引擎、INT8字节数据类型和
   `CcuHalfAll2AllVMesh1D`算法配置。
5. 通过`Mc2AcquireCcResCtx`申请CCU资源上下文`ccResCtx`及其大小。
6. 释放MC2参数对象，并通过`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)`启动CCU Server。
   `stream`参数为AICPU通路预留，当前CCU通路不使用。
7. 通过`all_to_all_v_write_kernel<<<1, nullptr, streamAiv>>>`启动AICore kernel。kernel内部
   调用`hccl.InitV2(contextGM)`，其中`contextGM`就是Host侧获取的`ccResCtx`，然后调用
   `Hccl::AlltoAllvWrite<true>`、`Wait`和`SyncAll`。
8. 同步AICore Stream，将`ccResCtx`拷回Host侧，取得`OpResCtx.res[0]`指向的scratch地址，
   再将scratch中的结果拷回Host并按源rank校验。
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

两卡场景下，rank `d`发往目标rank `j`的数据块首元素为`d * CCU_MAX_RANK_SIZE + j`，
运行成功后每个rank会打印发送块、接收块摘要和`validation: PASS`。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`同时传给`Mc2CcKernelLaunch`和AICore
  `all_to_all_v_write_kernel`，二者必须使用同一个资源上下文。
- 运行前请确认使用的`mc2_client`已支持`HCCL_CMD_HALF_ALLTOALLV`、算法
  `CcuHalfAll2AllVMesh1D`以及对应的CCU executor；如果当前devkit只定义了枚举而未完成这些注册，
  `Mc2AcquireCcResCtx`会在资源申请阶段返回不支持。
- `AlltoAllvWrite`的`sendSizes/sendOffsets`和`remoteWinOffset/localDataSize`均以字节为单位。
- 当前样例通过`HcclApi::OpResCtx`读取950 CCU MC2上下文中的`res[0]`，用于验证CCU scratch
  window结果；这不是`GetWindowsOutAddr`接口。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 当前样例使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`（8）。
