# CCU Direct AllReduce样例

## 概述

本样例展示如何基于HCCL通信域和CCU数据面接口，以直调`<<<>>>`方式实现AllReduce SUM。

所有rank的输入逐元素求和，每个rank都得到相同的求和结果。

通过编译参数`SCENARIO_NUM`选择教学场景：

- 场景1（推荐）：经典AllReduce SUM流程（`write_reduce`散射归约），每个rank的output为所有rank输入的逐元素和。
- 场景2（性能和扩展性不如场景1，主要用作接口示例）：独立的"聚合-归约"流程——先`read`把各rank输入搬运到本地`ccu_buffer`，再`local_reduce`（N路`ccu_buffer`重载）本地归约，输出与场景1相同。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## 目录结构介绍

```text
04_allreduce
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
├── run.sh            // 一键构建运行脚本
└── main.asc          // Host侧流程和CCU AllReduce Kernel实现
```

## 样例描述

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU AllReduce |
| 调用方式 | CCU Kernel直调`<<<>>>` |
| 教学场景 | 通过编译参数`SCENARIO_NUM`选择，取值1/2 |
| 数据类型 | FP32（`HCCL_DATA_TYPE_FP32`） |
| 归约操作 | SUM（`HCCL_REDUCE_SUM`） |
| 单rank输入规模 | `rankSize`个FP32元素 |
| 输出规模 | `rankSize`个FP32元素，所有rank结果相同 |
| 支持rank数 | 不超过`CCU_MAX_RANK_SIZE`，当前样例中为16 |

### 实现流程

场景1（经典AllReduce SUM，`write_reduce`散射归约）：

1. 初始化ACL和HCCL通信域，获取当前环境中的NPU数量。
2. 每个Device创建一个rank，输入元素`i`初始化为`device * rankSize + i`。
3. 基于HCCL通信域申请CCU Channel和CCU实例资源。
4. 通过`asccomm_ccu_get_mem_token`获取输入内存Token，并准备CCU任务参数。
5. 通过`CcuAllReduceMesh1DMem2MemKernel<<<schd, insHandle, stream>>>`直调CCU Kernel。
6. Kernel内先通过`ccu::local_copy`将本地输入复制到本地输出；同步后通过`ccu::write_reduce`将本地
   输入按SUM累加写入其它rank的输出，并等待全部远端写入完成。
7. 同步Stream后将结果拷贝回Host侧并打印。
8. 销毁HCCL通信域、Stream和Device侧内存。

场景2（独立的"聚合-归约"流程）：

1. 前同步仅交换本端input地址与token（无远端写，不交换output地址）。
2. 本端input经`ccu::local_copy`拷入`bufs[rankId]`。
3. 逐个对端`ccu::read`：对端input -> `bufs[对端rankId]`，把各rank数据搬运到本地。
4. `ccu::local_reduce`（N路`ccu_buffer`重载）把`bufs[0..N-1]`归约到`bufs[0]`。
5. `bufs[0]`拷回本端output，结果与场景1相同（全rank逐元素和）。

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

  在本样例目录下执行如下命令。将`SCENARIO_NUM`分别设置为`1`、`2`可以编译并运行两个教学场景。

  ```bash
  SCENARIO_NUM=1 bash run.sh
  ```

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR&950DT系列产品 |
  | `SCENARIO_NUM` | `1`/`2` | 教学场景：1经典AllReduce SUM、2独立聚合-归约流程，默认为`1` |

- 执行结果

  两卡场景下，rank `d`的输入元素`i`初始化为`d * rankSize + i`。两个场景的预期输出相同：

  场景1（默认）：

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 0 1 ]
  rankId: 1, input: [ 2 3 ]
  rankId: 0, output: [ 2 4 ]
  rankId: 1, output: [ 2 4 ]
  ```

  场景2采用独立的"聚合-归约"流程，输出与场景1相同：

  ```text
  Found 2 NPU device(s) available
  rankId: 0, input: [ 0 1 ]
  rankId: 1, input: [ 2 3 ]
  rankId: 0, output: [ 2 4 ]
  rankId: 1, output: [ 2 4 ]
  ```

  每个rank的输出均为所有rank输入的逐元素和，表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 场景2的`ccu_buffer`归约重载支持2~8路（rank数即设备数），设备数超过8时场景2不支持；场景1不受影响。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
