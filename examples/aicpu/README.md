# AICPU样例

简体中文 | [English](./README_en.md)

本目录提供AICPU侧通信样例，覆盖MC2 builtin接入和自定义AICPU通信算子两类开发方式。

## 样例列表

| 目录名称 | 内容说明 |
| --- | --- |
| [aicpu_builtin](./aicpu_builtin/01_allgather/README.md) | MC2 builtin接入样例。Host侧准备AICPU通信资源并启动KFC Server，AICore Kernel通过HCCL高阶API提交通信任务。 |
| [aicpu_direct](./aicpu_direct/README.md) | 自定义AICPU通信算子样例。Host侧通过HCCL通信编程接口申请资源，AICPU Kernel通过asc-comm POD数据面原语实现集合通信。 |

## 运行约束

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 自定义算子样例需配套asc-comm/hcomm源码环境，并完成AICPU包验签和白名单配置后运行。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。

