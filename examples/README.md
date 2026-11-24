# asc-comm样例

简体中文 | [English](./README_en.md)

本目录提供asc-comm API的使用样例。各目录层级的README介绍该层级的样例。

## 样例列表

| 目录名称 | 内容说明 |
| --- | --- |
| [aicore](./aicore/README.md) | AI Core侧通信样例，包含CommRW点对点通信样例（AIV直驱URMA、SIMT URMA、RoCE路径）、AIN单边通信样例和Jetty通信样例。 |
| [aicpu_ts](./aicpu_ts/README.md) | AICPU_TS通信样例，覆盖AICPU_TS MC2 builtin接入和自定义AICPU_TS通信算子两类开发方式。 |
| [ccu](./ccu/README.md) | CCU通信和数据面开发样例，包含direct（自定义算子）和builtin（MC2）两类样例，覆盖AllGather、AllReduce、ReduceScatter、AlltoAll等集合通信及AllGather+Add通算融合。 |

## 运行约束

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。

具体样例的编译命令、执行结果和注意事项参见对应目录下的README。
