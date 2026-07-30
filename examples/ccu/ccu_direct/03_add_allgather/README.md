# CCU Direct Add + AllGather样例

## 概述

本样例展示如何先通过AICore vector kernel执行Add计算，再以计算结果作为输入，通过CCU以直调`<<<>>>`
方式执行AllGather通信。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。每个rank先对本地输入执行AICore
Add计算，再将计算结果通过CCU AllGather同步到所有rank。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | >= CANN 9.1.0 |

## 目录结构介绍

```text
03_add_allgather
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
└── main.asc          // Host侧流程、AICore Add Kernel和CCU AllGather Kernel实现
```

## 样例描述

### 功能说明

每个rank的输入为`sendCount`个FP32元素。样例先通过AICore vector kernel将本地输入写入`computeBuf`，
再以`computeBuf`作为CCU AllGather输入，将所有rank的计算结果同步到每个rank的`recvBuf`。

```text
sendBuf -> AICore Add -> computeBuf -> CCU AllGather -> recvBuf
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU AllGather |
| 调用方式 | AICore Kernel直调`<<<>>>`，CCU Kernel直调`<<<>>>` |
| 数据类型 | FP32 |
| 单rank输入规模 | 256个FP32元素 |
| Add输出规模 | 256个FP32元素 |
| AllGather输出规模 | `rankSize * 256`个FP32元素 |
| 支持rank数 | 不超过`CCU_MAX_RANK_SIZE`，当前样例中为16 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，并初始化输入Buffer。
3. 基于HCCL通信域申请CCU Channel、CCU实例、CCU变量和CCU事件资源。
4. 通过`vector_add<<<1, nullptr, streamAiv>>>`直调AICore vector kernel，生成本地计算结果。
5. 通过`HcommCcuGetMemToken`获取`computeBuf`内存Token，并准备CCU任务参数。
6. 通过`CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, streamCcu>>>`直调CCU Kernel完成AllGather。
7. 同步AIV和CCU Stream后将`recvBuf`拷贝回Host侧并打印。
8. 销毁HCCL通信域、Stream和Device侧内存。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  请根据当前环境上CANN开发套件包的安装方式配置环境变量，并开启CCU调度模式。

  ```bash
  source ${install_path}/cann/set_env.sh
  export HCCL_OP_EXPANSION_MODE=CCU_SCHED
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

  两卡场景下，rank `d`的输入元素初始化为`d + 1`。运行成功后终端会输出类似以下信息：

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 1 1 1 ... ]
  rankId: 1, input: [ 2 2 2 ... ]
  rankId: 0, recvBuf: [ 2 2 2 ... 3 3 3 ... ]
  rankId: 1, recvBuf: [ 2 2 2 ... 3 3 3 ... ]
  ```

  每个rank的`recvBuf`均包含所有rank执行AICore Add后的结果，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅覆盖Ascend 950PR/Ascend 950DT场景。
- 当前样例会使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`。
