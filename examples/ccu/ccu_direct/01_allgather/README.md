# CCU Direct AllGather样例

## 概述

本样例展示如何基于HCCL通信域和CCU数据面接口，以直调`<<<>>>`方式实现AllGather。

样例会根据当前环境中的NPU数量创建通信域，每个Device对应一个rank。每个rank输入`rankSize`个FP32元素，元素值初始化为rank id。AllGather完成后，每个rank的输出Buffer按rank顺序保存所有rank输入。

运行时依赖：

- `libhcomm.so`：提供HCCL通信域、Channel和CCU实例查询接口。
- `libasccomm_ccu.so`：提供CCU直调、内存Token和CCU数据面编程接口。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## 目录结构介绍

```text
01_allgather/
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
├── run.sh            // 一键构建运行脚本
└── main.asc          // Host侧流程和CCU AllGather Kernel实现
```

## 样例描述

### 功能说明

每个rank的输入为`rankSize`个FP32元素：

```text
rank r sendBuf = [r, r, ..., r]
rank p recvBuf = [segment_0 | segment_1 | ... | segment_N-1]
```

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU AllGather |
| 调用方式 | CCU Kernel直调`<<<>>>` |
| 数据类型 | FP32 |
| 单rank输入规模 | `rankSize`个FP32元素 |
| 输出规模 | `rankSize * rankSize`个FP32元素 |
| 支持rank数 | 不超过`CCU_MAX_RANK_SIZE`，当前样例中为16 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，并初始化输入Buffer。
3. 基于HCCL通信域申请CCU Channel和CCU实例资源。
4. 通过`asccomm_ccu_get_mem_token`获取输入内存Token，并准备CCU任务参数。
5. 通过`CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, stream>>>`直调CCU Kernel。
6. 同步Stream后将结果拷贝回Host侧并打印。
7. 销毁HCCL通信域、Stream和Device侧内存。

## 编译运行

执行前先设置CANN运行环境和CCU调度模式：

```bash
source ${install_path}/cann/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
```

在本样例目录下执行：

```bash
bash run.sh
```

等价的手动命令为：

```bash
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
cmake --build . -j$(nproc)
cd ..
exec build/ccu_allgather
```

## 执行结果

两卡场景下，运行成功后终端输出类似以下信息：

```text
Found 2 NPU device(s) available
rankId: 0, input: [ 0 0 ]
rankId: 1, input: [ 1 1 ]
rankId: 0, recvBuf: [ 0 0 1 1 ]
rankId: 1, recvBuf: [ 0 0 1 1 ]
```

每个rank的`recvBuf`均包含所有rank输入，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
- 当前样例会使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`。
