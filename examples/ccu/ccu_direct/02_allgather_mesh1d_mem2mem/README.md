# CCU Direct AllGather Mesh1D Mem2Mem样例

## 概述

本样例展示如何以直调`<<<>>>`方式下发CCU Kernel，实现Mesh1D Mem2Mem AllGather。

所有rank的输入按rank顺序拼接，每个rank都得到一份完整的数据。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## 目录结构介绍

```text
02_allgather_mesh1d_mem2mem
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
├── run.sh            // 一键构建运行脚本
├── main.asc          // Host侧流程和Mesh1D Mem2Mem AllGather Kernel实现
└── scripts
    ├── gen_data.py       // 输入和Golden数据生成脚本
    └── verify_result.py  // 输出校验脚本
```

## 样例描述

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU Mesh1D Mem2Mem AllGather |
| 调用方式 | CCU Kernel直调`<<<>>>` |
| 数据类型 | FP32 |
| 单rank输入规模 | `COUNT`个FP32元素，默认256 |
| 输出规模 | `rankSize * COUNT`个FP32元素 |
| 支持rank数 | 1～16，不超过`CCU_MAX_RANK_SIZE` |

### 实现流程

1. 初始化ACL和HCCL通信域，rank数量由`DEVICES`指定的Device数量决定。
2. 每个rank从输入文件读取数据，并拷贝到Device侧输入Buffer。
3. 基于HCCL通信域申请CCU Channel和CCU实例资源。
4. 通过`asccomm_ccu_get_mem_token`获取输入内存Token，并准备CCU任务参数。
5. 通过`CcuAllGatherMesh1DMem2MemKernel<<<schd, insHandle, stream>>>`直调CCU Kernel。
6. Kernel内通过`ccu::write_variable_with_notify`和`ccu::notify_wait`交换各rank的输出地址和Token；
   随后通过`ccu::write`向所有对端写入本rank的slice，通过`GroupCopy`完成本地slice拷贝，并使用
   按rank分组的Event等待全部传输完成。
7. 同步Stream后将结果拷贝回Host侧并打印。
8. 销毁HCCL通信域、Stream和Device侧内存。

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  请根据当前环境上CANN开发套件包的安装方式配置开发环境，并开启CCU调度模式。

  ```bash
  source ${install_path}/cann/set_env.sh
  export HCCL_OP_EXPANSION_MODE=CCU_SCHED
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 样例执行

  生成输入和Golden数据。数据生成和校验脚本依赖Python 3和NumPy。

  ```bash
  python3 scripts/gen_data.py --rank-size 2 --count 256
  ```

  构建并运行样例：

  ```bash
  env DEVICES=0,1 COUNT=256 ./run.sh
  ```

  校验各rank的输出：

  ```bash
  python3 scripts/verify_result.py --rank-size 2 --count 256
  ```

  `--rank-size`必须与`DEVICES`指定的Device数量一致，`--count`必须与运行样例时的`COUNT`一致。
  `run.sh`只负责构建和运行样例，不会自动生成或校验数据；仅构建时可执行`env BUILD_ONLY=1 ./run.sh`。
  每次修改rank数量或`count`后，都需要使用相同参数重新生成、运行和校验数据。

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR&950DT系列产品 |

- 执行结果

  以下为2个rank、每个rank输入256个FP32元素时的部分输出。不同rank的打印顺序可能因线程调度而变化。

  ```text
  generated 2 rank inputs and golden data, 256 FP32 elements per rank
  rank 1: launched, waiting for stream
  rank 0: launched, waiting for stream
  rank 0: stream completed
  rank 1: stream completed
  rank 1 input : [ 1000001 1000002 1000003 1000004 1000005 1000006 1000007 1000008 1000009 1000010 1000011 1000012 1000013 1000014 1000015 1000016 ...(256) ]
  rank 1 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
  rank 1: output saved to <sample_path>/output/output_rank_1.bin
  rank 0 input : [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(256) ]
  rank 0 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
  rank 0: output saved to <sample_path>/output/output_rank_0.bin
  rank 0 verify pass
  rank 1 verify pass
  test pass!
  ```

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
