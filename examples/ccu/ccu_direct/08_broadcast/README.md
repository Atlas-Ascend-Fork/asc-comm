# CCU Direct Broadcast样例

## 概述

本样例展示如何基于HCCL通信域和CCU数据面接口，以直调`<<<>>>`方式实现Broadcast。

root rank将完整输入广播给所有rank。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## 目录结构介绍

```text
08_broadcast
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
├── run.sh            // 一键构建运行脚本
└── main.asc          // Host侧流程和CCU Broadcast Kernel实现
```

## 样例描述

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU Broadcast |
| 调用方式 | CCU Kernel直调`<<<>>>` |
| root rank | 0 |
| 数据类型 | FP32 |
| 单rank输入规模 | `rankSize`个FP32元素 |
| 单次数据量 | 不超过256 MiB |
| 支持rank数 | 不超过`CCU_MAX_RANK_SIZE`，当前样例中为16 |

### 实现流程

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，root的输入初始化为元素下标`i`，非root的输入初始化为`-1`。
3. 基于HCCL通信域申请CCU Channel和CCU实例资源。
4. 通过`asccomm_ccu_get_mem_token`获取Buffer内存Token，并准备CCU任务参数。
5. 通过`CcuBroadcastMesh1DKernel<<<schd, insHandle, stream>>>`直调CCU Kernel。
6. Kernel内先通过`ccu::write_variable_with_notify`和`ccu::notify_wait`交换各rank的Buffer地址和Token；
   随后root rank通过`ccu::write`将完整数据写入其他rank，并等待全部远端写入完成。
7. 同步Stream后将结果拷贝回Host侧，校验输出等于root的输入，打印`PASS`或`FAIL`。
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

  在本样例目录下执行如下命令。

  ```bash
  bash run.sh
  ```

  `run.sh`会完成CMake配置、编译并运行样例。

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR&950DT系列产品 |

- 执行结果

  两卡场景下，root rank 0的输入为`[0, 1]`。运行成功后终端会输出类似以下信息：

  ```text
  Found 2 NPU device(s) available
  rankId: 0, output: [ 0 1 ]
  rankId: 0 PASS
  rankId: 1, output: [ 0 1 ]
  rankId: 1 PASS
  ```

  每个rank均输出`PASS`，表示样例执行成功。若任意rank输出`FAIL`，表示执行结果与预期不一致。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
