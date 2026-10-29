# asc-comm 样例

简体中文 | [English](./README_en.md)

本目录提供asc-comm API的使用样例。

## 样例列表

| 目录名称 | 内容说明 |
| --- | --- |
| [aicore](./aicore/README.md) | AICore侧通信样例，包含Hcomm点对点通信样例（AIV直驱URMA、SIMT URMA、RoCE路径）、Ain单边通信样例和Jetty通信样例。 |
| [ccu](./ccu/README.md) | CCU通信和数据面开发样例，覆盖builtin接入、直调集成和自定义Kernel三类开发方式。 |

## 运行约束

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。

具体样例的编译命令、执行结果和注意事项参见对应目录下的README。
