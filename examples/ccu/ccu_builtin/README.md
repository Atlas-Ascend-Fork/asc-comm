# CCU Builtin样例

简体中文 | [English](./README_en.md)

本目录提供通过MC2 builtin接口使用CCU的样例。Host侧负责资源预检查、申请CCU资源上下文并启动CCU Server，AICore Kernel通过HCCL高阶API提交通信Client任务。

## 样例列表

| 样例 | 说明 |
| --- | --- |
| [01_allgather](./01_allgather/README.md) | 演示CCU AllGather通信任务。 |
| [02_allgather_add](./02_allgather_add/README.md) | 演示AllGather通信与AICore Add计算的组合。 |
| [03_allreduce](./03_allreduce/README.md) | 演示CCU AllReduce SUM通信任务。 |
| [04_reducescatter](./04_reducescatter/README.md) | 演示CCU ReduceScatter通信任务。 |
| [05_all2all](./05_all2all/README.md) | 演示等长AllToAll数据交换。 |
| [06_all2allv](./06_all2allv/README.md) | 演示可变长度AllToAllV数据交换。 |

## 使用说明

- 样例支持Ascend 950PR&950DT系列产品，CANN版本要求为9.2.0或以上。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 样例会使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
