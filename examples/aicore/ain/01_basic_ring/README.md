# AIN Basic Ring样例

## 概述

本样例展示如何在Ascend C AIV Kernel中通过`ain/ain.h`接口使用AIN基础通信能力。样例采用多rank
对称执行方式：每个rank向下一个rank执行`Put`，并对上一个rank执行`Get`，最后通过
`AinBarrierSession`完成同步。

Host侧负责内存申请、初始化HCCL通信域、创建HCCL Team、注册对称window并创建AIV通信channel；
Kernel侧只通过AIN接口提交数据面通信任务。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
basic_ring
├── CMakeLists.txt    // 编译工程文件
├── basic_ring.asc    // Host侧资源准备、AICore侧AIN调用和共享定义
├── README.md         // 中文样例说明
└── README_en.md      // 英文样例说明
```

## 样例描述

### 功能说明

`nranks`个rank组成环形拓扑，当前rank `r`执行两次单边通信：

- `Put`：将本rank标识写入下一个rank（`(r + 1) % nranks`）的`recvWindow[rankId]`
- `Get`：从上一个rank（`(r - 1 + nranks) % nranks`）的`sendWindow[0]`读取数据到本rank的
  `recvWindow[rankNum + rankId]`

通信完成后每个rank回读`recvBuf`并校验`Put`和`Get`的结果。

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | 环形拓扑AIN点对点单边通信 |
| 通信引擎 | `COMM_ENGINE_AIV`，协议`COMM_PROTOCOL_UB_CTP` |
| 部署形态 | 单机多卡 |
| 支持rank数 | >= 2，且不超过当前可用NPU数量 |

### 实现流程

1. 可执行文件直接拉起全部rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换root info并
   创建多rank HCCL通信域。
2. 每个rank初始化ACL运行环境，通过`HcclTeamCreateDescInit`配置team描述（包括`barrierCount`、
   `rankIds`、通信引擎`COMM_ENGINE_AIV`、协议`COMM_PROTOCOL_UB_CTP`和`channelCnt`），
   调用`HcclTeamCreate`创建team并自动建立AIV + UB_CTP通信channel。
3. 分别将`sendBuf`和`recvBuf`通过`HcclCommSymWinRegister`注册为对称window。
4. 下发`team`、`sendWin`、`recvWin`到Kernel。
5. Kernel侧构造`AscendC::Ain`对象并初始化Hcomm UB临时工作区，执行`Put`和`Get`后调用`Flush`
   等待通信任务完成，最后通过`AinBarrierSession::Sync`完成rank间同步。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  请根据当前环境上CANN开发套件包的安装方式配置环境变量。

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
  cd .. && ./build/basic_ring_demo tcp://127.0.0.1:29622 <nranks>
  ```

- 启动参数说明

  | 参数 | 默认值 | 说明 |
  | --- | --- | --- |
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换；并行运行多个实例时错开端口 |
  | `<nranks>` | 无默认 | 启动的rank数量，必须为不小于2的整数，且不超过当前可用NPU数量 |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR/Ascend 950DT |

- 执行结果

  每个rank会回读`recvBuf`并打印`Put`和`Get`结果，例如：

  ```text
  [rank 0] basic_ring | PUT recvBuf[1]=1, GET recvBuf[2]=1 | PASS
  [rank 1] basic_ring | PUT recvBuf[0]=0, GET recvBuf[3]=0 | PASS
  RESULT | Example=basic_ring | Status=PASS
  ```

  所有rank均输出`PASS`且最终输出`Status=PASS`表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 编译和运行前必须先加载CANN环境变量，确保`ASCEND_CANN_PACKAGE_PATH`、ASC CMake模块和运行时
  动态库可用。
- 样例依赖CANN中的`hccl`、`hcomm`、`ascendcl`和`runtime`库。
- 运行时rank数量不能超过当前可用NPU数量。
- 样例仅支持单机多卡运行，不支持跨节点多机运行。
