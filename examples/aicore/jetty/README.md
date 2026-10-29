# Jetty样例

简体中文 | [English](./README_en.md)

本目录提供Jetty通信样例。Jetty样例通过`jetty/hcomm_jetty.h`和`jetty/hcomm_jetty_simt.h`接口直接向Jetty SQ提交WQE，分别覆盖AIV Kernel直驱和SIMT Kernel两种执行模型。

## 样例列表

| 样例 | 说明 |
| --- | --- |
| [01_hcomm_jetty_write](./01_hcomm_jetty_write/README.md) | 演示多卡场景下AIV Kernel通过`HcommJetty::Write`和`HcommJetty::WriteValue`直接向Jetty SQ提交WQE，并通过`Drain`等待完成与校验结果。 |
| [02_simt_hcomm_jetty](./02_simt_hcomm_jetty/README.md) | 演示多卡场景下SIMT Kernel以thread/warp/group协作方式通过`HcommJetty::Write`直接构造并提交Jetty WQE，覆盖dispatch多warp批量提交与SIMT/SIMD混合发布模式，支持正确性压测与设备侧计时。 |

## 使用说明

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 样例编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
