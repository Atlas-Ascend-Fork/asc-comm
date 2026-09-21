# Hcomm Jetty Write样例

## 概述

本样例演示如何在AIV Kernel中直接向Jetty SQ提交WQE，完成多卡间的点对点单边通信。样例采用多rank
对称执行方式：每个rank向其余所有rank各执行一次`HcommJetty::Write`和一次`HcommJetty::WriteValue`，
再通过`HcommJetty::Drain`等待整批WQE完成。Host侧准备通信域、注册内存并建立URMA Channel，数据面
执行期间不需要Host逐次参与；各rank随后校验收到的数据与marker，主进程最后汇总所有rank的执行结果。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
01_hcomm_jetty_write
├── CMakeLists.txt          // 编译工程文件
├── hcomm_jetty_write.asc   // Host侧资源准备、AIV Kernel侧Jetty调用与共享定义
├── README.md               // 中文样例说明
└── README_en.md            // 英文样例说明
```

## 样例描述

### 功能说明

每个rank把本端一段数据写入其余所有rank的接收槽位，并向独立的marker地址写入一个标识值。Kernel侧
用`HcommPeer`和`HcommJetty`直接构造并提交WQE：`Write`搬运256字节数据，`WriteValue`写入8字节
marker，`Drain`等待本rank提交的全部WQE完成。所有rank的写入落地后，各rank校验来自每个peer的数据
槽与marker是否与约定值一致。

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | 全体rank两两互写的多对多点对点通信 |
| 通信协议 | `COMM_PROTOCOL_UBC_CTP` |
| 通信引擎 | `COMM_ENGINE_AIV` |
| 调用方式 | AIV Kernel内`HcommJetty::Write`/`WriteValue`/`Drain` |
| 数据校验 | 每个rank校验来自所有peer的数据槽与WriteValue marker |
| 支持rank数 | 2至16，且不超过本机可用NPU数量 |

### 实现流程

1. 可执行文件直接拉起全部rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换root info并
   创建通信域。
2. 各rank准备待发送数据、注册通信内存，并为每个peer构造`COMM_PROTOCOL_UBC_CTP`的Channel描述后
   建立Channel。
3. 各rank把全部Jetty handle写入一张device侧表，并按peer下发context：本端buffer地址、远端数据
   槽地址、远端marker地址与marker值。
4. 全体建链完成后下发kernel，Kernel侧逐个peer调用`Write`、`WriteValue`，并用`Drain`等待完成。
5. 各rank回读通信buffer，校验所有peer写入的数据槽与marker，主进程汇总所有rank的执行结果。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  请根据当前环境上CANN开发套件包的安装方式配置环境变量。

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 样例执行

  在本样例目录下执行如下命令。可执行文件内部fork出全部rank进程，直接运行即可。

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd .. && ./build/hcomm_jetty_write tcp://127.0.0.1:29627 <rank_num>
  ```

- 启动参数说明

  | 参数 | 默认值 | 说明 |
  | --- | --- | --- |
  | `<ip:port>` | 无默认 | rank 0监听的控制通道地址（`tcp://ip:port`格式），用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `<rank_num>` | 无默认 | 启动的rank数量，取值范围`2-16`，不能超过本机可用NPU数量 |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR/Ascend 950DT |

- 执行结果

  2卡运行成功时，每个rank输出校验通过信息，主进程最终输出：

  ```text
  [rank 0] hcomm_jetty_write | Write and WriteValue verified for 1 peer(s) | PASS
  [rank 1] hcomm_jetty_write | Write and WriteValue verified for 1 peer(s) | PASS
  RESULT | Example=hcomm_jetty_write | Status=PASS
  ```

  主进程输出`Status=FAIL`或任一rank报错时，表示本次Jetty写入或数据校验未通过。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- rank数量不能超过本机可用NPU数量，且运行用户需具备设备访问权限。
- 样例仅支持单机多卡运行，不支持跨节点多机运行。
- 编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
