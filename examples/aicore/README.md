# AICore样例

简体中文 | [English](./README_en.md)

本目录提供AICore侧通信API使用样例，按接口分为Hcomm点对点通信、Ain单边通信和Jetty通信三类；`utils`目录提供各样例共享的Host侧工具代码，不是独立样例。

## 样例列表

| 目录名称 | 内容说明 |
| --- | --- |
| [ain](./ain/README.md) | Ain单边通信样例。演示基于对称窗口的`Put`/`Get`单边通信与集合同步。 |
| [hcomm](./hcomm/README.md) | Hcomm点对点通信样例。覆盖AIV直驱URMA、SIMT URMA和RoCE路径的普通接口、批量接口、多SGE离散读写，以及功能验证与性能测试。 |
| [jetty](./jetty/README.md) | Jetty通信样例。演示AIV Kernel直驱和SIMT Kernel直接向Jetty SQ构造并提交WQE。 |

## 使用说明

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
