# CCU Builtin Check后执行AllGather样例

## 概述

实际通信中，CCU资源不足的问题往往要等kernel跑起来才会暴露。本样例演示`CheckOpResSufficient`
的用法：发起AllGather前先预校验一次设备CCU资源，返回`HCCL_SUCCESS`才继续按
[01_allgather](../01_allgather)的流程真实执行AllGather并校验结果；check失败（如资源不足
返回1043）则用户自行决定其他操作。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.2.0 |

## 目录结构介绍

```text
07_check_then_allgather
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧check流程、CCU资源准备和AICore通信Client实现
```

## 样例描述

### 功能说明

每个rank的输入为`sendCount`个FP32元素。Host侧先构造与真实通信一致的MC2参数对象并调用
`CheckOpResSufficient`做资源校验；成功后申请CCU资源上下文、启动CCU Server，AICore侧
`all_gather_kernel`通过`Hccl::AllGather`将各rank输入按照rank id顺序聚合到每个rank的
`recvBuf`，Host侧逐元素校验结果。

```text
Host:
HcclComm
  -> Mc2GetCcArgs/Mc2SetCc* (CCU_SCHED + FP32 + CcuSchedAllGatherSoleMesh)
  -> CheckOpResSufficient   (资源校验，资源充足才继续)
  -> Mc2AcquireCcResCtx
  -> Mc2CcKernelLaunch      (start CCU Server)

AICore:
ccResCtx
  -> Hccl::InitV2
  -> Hccl::AllGather (submit CCU Client request)
  -> recvBuf
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU AllGather |
| 资源校验 | Host侧调用`CheckOpResSufficient(comm, ccType, ccArgs)`，参数与后续Acquire一致 |
| CCU Server启动 | Host侧调用`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)` |
| 通信任务提交 | AICore侧调用`Hccl::AllGather<true>` |
| AICore Kernel | `all_gather_kernel<<<1, nullptr, streamAiv>>>` |
| 数据类型 | FP32 |
| 单rank输入规模 | 256个FP32元素 |
| AllGather输出规模 | `rankSize * 256`个FP32元素 |
| 结果校验 | 逐元素校验`recvBuf`第rank段等于rank+1 |
| 支持rank数 | 不超过`CCU_MAX_RANK_SIZE`，当前样例中为16 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，创建AICore Stream并申请输入和接收Buffer。
3. 通过`Mc2GetCcArgs`创建MC2参数对象，设置`CCU_SCHED`通信引擎、FP32源/目标数据类型和
   `CcuSchedAllGatherSoleMesh`算法配置。
4. 调用`CheckOpResSufficient(comm, HCCL_CMD_ALLGATHER, ccArgs)`校验资源：返回`HCCL_SUCCESS`
   说明资源充足，继续后续流程；返回非0（如资源不足1043）则不发起通信，直接返回错误。
5. check通过后通过`Mc2AcquireCcResCtx`申请CCU资源上下文`ccResCtx`，释放MC2参数对象，
   并通过`Mc2CcKernelLaunch(nullptr, ccResCtx, ccResCtxSize)`启动CCU Server。
6. 通过`all_gather_kernel<<<1, nullptr, streamAiv>>>`启动AICore kernel，kernel内部调用
   `hccl.InitV2(contextGM)`，其中`contextGM`就是Host侧获取的`ccResCtx`。
7. AICore侧调用`Hccl::AllGather<true>`提交通信Client任务，调用`Wait`和`SyncAll`等待通信完成。
8. 同步AICore Stream，将`recvBuf`拷贝回Host侧，逐元素校验（第rank段等于rank+1）并打印结果。
9. 销毁HCCL通信域、Stream和Device侧内存。`ccResCtx`由通信域管理，不需要单独释放。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

- 样例执行

  在本样例目录下执行如下命令。

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  ./demo
  ```

- 执行结果

  两卡场景下，rank `d`的输入元素初始化为`d + 1`。运行成功后终端会输出类似以下信息：

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  CheckOpResSufficient ret=0, resource sufficient, start AllGather
  CheckOpResSufficient ret=0, resource sufficient, start AllGather
  rankId: 0, recvBuf: [ 1 1 1 ... 2 2 2 ... ]
  rankId: 1, recvBuf: [ 1 1 1 ... 2 2 2 ... ]
  rankId: 0, result check: PASS
  rankId: 1, result check: PASS
  ```

  每个rank先打印check成功的提示，再输出聚合后的`recvBuf`和校验PASS结果，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU。
- check与`Mc2AcquireCcResCtx`必须使用同一份参数内容，参数不一致时校验结果对真实通信无参考意义。
- `Mc2AcquireCcResCtx`返回的`ccResCtx`同时传给`Mc2CcKernelLaunch`和AICore
  `all_gather_kernel`，二者必须使用同一个资源上下文。
- AICore通信任务需要在CCU Server启动后提交，`Hccl::AllGather`不是Host侧直接执行的AllGather。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 当前样例会使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`。
