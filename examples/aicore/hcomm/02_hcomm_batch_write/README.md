# Hcomm Batch Write样例

## 概述

本样例演示如何使用Hcomm批量写接口，在单机多卡环境中由rank 0向其他rank批量写入数据。多个URMA
Channel共享同一个Jetty，通过`MakeBatchHandle`、`GetHandleRef`、`BatchCommit`和`Drain`批量提交
跨peer写任务。程序会准备待发送数据，提交一批写操作并等待完成；接收rank随后校验收到的数据，主进程
最后汇总所有rank的执行结果。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
hcomm_batch_write
├── CMakeLists.txt               // 编译工程文件
├── hcomm_batch_write.asc        // Host侧资源准备与Kernel调用
├── hcomm_batch_write_kernel.cpp // AIV Kernel侧批量写调用
├── hcomm_batch_write_def.h      // Host与Kernel共享定义
├── README.md                    // 中文样例说明
└── README_en.md                 // 英文样例说明
```

## 样例描述

### 功能说明

rank 0将一段本端数据通过批量写接口同时写入其他所有rank的接收缓冲区。批量提交将多个写任务打包为
一个batch handle，通过一次`BatchCommit`统一下发，减少提交次数；`Drain`等待整批任务完成后，各
接收rank校验落地的数据是否与本端发送数据一致。

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | rank 0到其他所有rank的一对多批量写 |
| 调用方式 | AIV Kernel内`MakeBatchHandle`/`GetHandleRef`/`BatchCommit`/`Drain` |
| 数据校验 | 每个接收rank校验落地数据 |
| 支持rank数 | 2至16，且不超过本机可用NPU数量 |

### 实现流程

1. 可执行文件直接拉起全部rank进程（进程内fork，参见`examples/aicore/utils/process_manager.h`）；
   rank间通过命令行指定的TCP控制通道（`examples/aicore/utils/rank_sync.h`）交换root info并
   创建通信域，rank 0准备待发送数据。
2. 各rank注册通信内存并通过控制通道交换缓冲区地址，rank 0与每个peer建立共享Jetty的URMA Channel。
3. Kernel侧构造batch handle，为每个peer填充一个写任务引用后统一`BatchCommit`。
4. `Drain`等待整批任务完成。
5. 各接收rank校验落地数据，主进程汇总所有rank的执行结果。

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
  cd .. && ./build/hcomm_batch_write tcp://127.0.0.1:29624 <rank_num>
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

  4卡运行成功时，每个接收rank输出校验通过信息，主进程最终输出：

  ```text
  [rank 1] hcomm_batch_write | received and verified data from rank 0 | PASS
  [rank 2] hcomm_batch_write | received and verified data from rank 0 | PASS
  [rank 3] hcomm_batch_write | received and verified data from rank 0 | PASS
  RESULT | Example=hcomm_batch_write | Status=PASS
  ```

  主进程输出`Status=FAIL`或任一rank报错时，表示本次批量写入或数据校验未通过。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- rank数量不能超过本机可用NPU数量，且运行用户需具备设备访问权限。
- 编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
