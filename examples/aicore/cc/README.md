# 集合通信样例（AICore发起）

简体中文 | [English](./README_en.md)

本目录提供由AICore发起的集合通信样例。通信任务由AICore侧发起：Host侧申请通信引擎资源并启动对应Server，AICore Kernel通过HCCL高阶API提交通信Client任务。按通信引擎分为AICPU_TS和CCU两类，当前覆盖MC2 builtin接入方式。

## 样例列表

| 目录名称 | 内容说明 |
| --- | --- |
| [aicpu_ts_builtin](./aicpu_ts_builtin/README.md) | AICPU_TS MC2 builtin接入样例。覆盖AllGather、AllGatherAdd、AllReduce、ReduceScatter、AlltoAll和AlltoAllV。 |
| [ccu_builtin](./ccu_builtin/README.md) | CCU MC2 builtin接入样例。覆盖AllGather、AllGatherAdd、AllReduce、ReduceScatter、AlltoAll和AlltoAllV。 |

## 使用说明

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- CCU builtin样例运行前必须显式设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`，以子样例README为准。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
