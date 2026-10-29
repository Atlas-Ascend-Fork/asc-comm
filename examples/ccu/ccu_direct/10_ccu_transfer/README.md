# CCU Direct数据搬运样例

## 概述

本样例展示CCU数据面的基础搬运、装载与控制流接口，覆盖七个教学场景。样例固定使用2个rank，每个rank
输入4个FP32元素，通过编译参数`SCENARIO_NUM`选择样例场景。

- 场景1调用`ccu::local_copy`的三个重载：本端片上内存 -> 本端片上内存、
  本端片上内存 -> 本端CCU Buffer、本端CCU Buffer -> 本端片上内存。
  其中"经CCU Buffer中转"的一路由`ccu::call_func`函数块完成：源/目的地址与token、长度共5个
  variable入参传入，函数块内自建address/ccu_buffer/event资源，首次调用合成函数块，指令只生成一份。
- 场景2调用`ccu::write`的两个重载：本端片上内存 -> 对端片上内存、本端CCU Buffer -> 对端片上内存。
- 场景3调用`ccu::write_reduce`，支持本端片上内存 -> 对端片上内存，远端写入时按元素执行SUM归约。
- 场景4调用`ccu::load`/`ccu::store`与`ccu::address`：`address`是独立地址资源，支持variable间接
  赋值并装入`local_addr`；`load`/`store`在variable与片上内存之间按地址搬运标量。
- 场景5调用`ccu::read`的两个重载：对端片上内存 -> 本端片上内存、对端片上内存 -> 本端CCU Buffer。
  注意read参数序为"目的在前，源在后"，与`ccu::write`相反；该场景会额外与对端交换输入地址。
- 场景6调用`ccu::read_reduce`：本端output先写入初值，再从对端读取并原地SUM归约，结果为两端输入之和。
- 场景7使用`CCU_DO`/`CCU_WHILE`/`CCU_IF`/`CCU_ELSE`软件控制流宏组织与场景1等价的三段搬运，
  并演示`event_record("tag", mask)`/`event_wait("tag", mask)`的local_notify重载
  （同die跨kernel同步）。

所有场景都使用Event跟踪搬运完成状态，rank之间通过Channel交换输出地址和Token，并在Kernel结束前同步
远端写入完成。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## 目录结构介绍

```text
10_ccu_transfer
├── CMakeLists.txt    // 编译工程文件
├── README.md         // 中文样例说明
├── README_en.md      // 英文样例说明
├── run.sh            // 一键构建运行脚本
└── main.asc          // Host侧流程和CCU数据搬运Kernel实现
```

## 样例描述

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | HCCL通信域内CCU数据搬运 |
| 调用方式 | CCU Kernel直调`<<<>>>` |
| 教学场景 | 通过编译参数`SCENARIO_NUM`选择，取值1~7 |
| 数据类型 | FP32 |
| 单rank输入规模 | 4个FP32元素 |
| 支持rank数 | 固定2个 |

### 实现流程

1. 初始化ACL和HCCL通信域，固定创建2个rank。
2. rank0输入初始化为`[1, 2, 3, 4]`，rank1输入初始化为`[5, 6, 7, 8]`。
3. 基于HCCL通信域申请CCU Channel和CCU实例资源。
4. 通过`asccomm_ccu_get_mem_token`获取输入和输出内存Token，并准备CCU任务参数。
5. 通过`CcuTransferKernel<<<schd, insHandle, stream>>>`直调CCU Kernel。
6. Kernel内先交换输出地址和Token，再按`SCENARIO_NUM`编译出的场景执行`ccu::local_copy`、`ccu::write`
   或`ccu::write_reduce`等操作，使用Event跟踪搬运完成状态并同步远端写入。
7. 同步Stream后将结果拷贝回Host侧，按场景预期校验并打印`PASS`或`FAIL`。
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

  在本样例目录下执行如下命令。将`SCENARIO_NUM`分别设置为`1`~`7`可以编译并运行七个教学场景。

  ```bash
  SCENARIO_NUM=1 bash run.sh
  ```

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR&950DT系列产品 |
  | `SCENARIO_NUM` | `1`~`7` | 教学场景：1本地拷贝、2远端写、3远端归约写、4load/store/address、5read、6read_reduce、7控制流宏+local_notify，默认为`1` |

- 执行结果

  rank0输入为`[1, 2, 3, 4]`，rank1输入为`[5, 6, 7, 8]`，七个场景的预期输出如下：

  | 场景 | 接口 | rank0输出 | rank1输出 |
  | --- | --- | --- | --- |
  | 1 | `local_copy`三个重载（中转一路经`call_func`函数块） | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |
  | 2 | `write`两个重载 | `[5, 6, 7, 8]` | `[1, 2, 3, 4]` |
  | 3 | `write_reduce` | `[6, 8, 10, 12]` | `[6, 8, 10, 12]` |
  | 4 | `load`/`store` + `address` | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |
  | 5 | `read`两个重载 | `[5, 6, 7, 8]` | `[1, 2, 3, 4]` |
  | 6 | `read_reduce` | `[6, 8, 10, 12]` | `[6, 8, 10, 12]` |
  | 7 | 控制流宏 + `local_notify` | `[1, 2, 3, 4]` | `[5, 6, 7, 8]` |

  每个rank都会输出`PASS`。若任意rank输出`FAIL`，说明搬运结果与预期不一致。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
