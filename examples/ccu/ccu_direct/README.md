# CCU Direct样例

简体中文 | [English](./README_en.md)

本目录提供ASC直调方式集成CCU的样例。样例通过`<<<>>>`启动CCU Kernel，并展示通信任务与AICore计算任务的前后组合方式。

## 样例列表

| 样例 | 说明 |
| --- | --- |
| [01_allgather](./01_allgather/README.md) | 演示CCU直调方式实现AllGather。 |
| [02_allgather_add](./02_allgather_add/README.md) | 先执行CCU AllGather，再对聚合结果执行AICore Add计算。 |
| [03_add_allgather](./03_add_allgather/README.md) | 先执行AICore Add计算，再通过CCU AllGather同步计算结果。 |

## 使用说明

- 样例支持Ascend 950PR/Ascend 950DT，CANN版本要求为9.1.0或以上。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 样例会使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
