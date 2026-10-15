# CCU样例

简体中文 | [English](./README_en.md)

本目录提供CCU通信和数据面开发样例，覆盖builtin接入、直调集成和自定义Kernel三类开发方式。

## 样例列表

| 目录名称 | 内容说明 |
| --- | --- |
| [ccu_builtin](./ccu_builtin/README.md) | MC2 builtin接入样例。Host侧申请CCU资源并启动CCU Server，AICore Kernel通过HCCL高阶API提交通信任务。 |
| [ccu_direct](./ccu_direct/README.md) | ASC直调集成样例。通过`<<<>>>`方式启动CCU Kernel，展示通信与AICore计算的组合方式。 |
| [ccu_custom](./ccu_custom/README.md) | 自定义CCU Kernel样例。Host侧直接注册并下发Kernel，展示集合通信和基础数据搬运接口的使用方法。 |

## 运行约束

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 直调和自定义Kernel样例运行前通常需要设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`，以子样例README为准。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
