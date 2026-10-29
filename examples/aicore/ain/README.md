# Ain样例

简体中文 | [English](./README_en.md)

本目录提供Ain单边通信样例。Ain面向算子Kernel侧，通过`AscendC::Ain`模板基于对称窗口发起`Put`/`Get`/`Signal`等单边操作。

## 样例列表

| 样例 | 说明 |
| --- | --- |
| [01_basic_ring](./01_basic_ring/README.md) | 演示多卡环形场景下AIV Kernel通过AIN接口调用`Put`/`Get`进行点对点单边通信，并通过`AinBarrierSession`完成同步与结果校验。 |

## 使用说明

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
