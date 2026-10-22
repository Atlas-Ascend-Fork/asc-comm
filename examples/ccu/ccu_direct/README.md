# CCU Direct样例

简体中文 | [English](./README_en.md)

本目录提供直调方式集成CCU的样例。样例通过`<<<>>>`启动CCU Kernel，覆盖基础集合通信、可变长度通信、基础数据搬运接口，以及通信任务与AICore计算任务的组合方式。

## 样例列表

| 样例 | 说明 |
| --- | --- |
| [01_allgather](./01_allgather/README.md) | 演示CCU直调方式实现AllGather。 |
| [02_allgather_mesh1d_mem2mem](./02_allgather_mesh1d_mem2mem/README.md) | 演示Mesh1D Mem2Mem AllGather数据面。 |
| [03_allgather_nhr1d_mem2mem](./03_allgather_nhr1d_mem2mem/README.md) | 演示NHR1D Mem2Mem AllGather数据面。 |
| [04_allreduce](./04_allreduce/README.md) | 演示AllReduce SUM。 |
| [05_all_to_all](./05_all_to_all/README.md) | 演示等长AllToAll。 |
| [06_all_to_allv](./06_all_to_allv/README.md) | 演示可变长度AllToAllV。 |
| [07_all_to_all_vc](./07_all_to_all_vc/README.md) | 演示基于count matrix的AllToAllVC。 |
| [08_broadcast](./08_broadcast/README.md) | 演示Broadcast。 |
| [09_reduce_scatter](./09_reduce_scatter/README.md) | 演示ReduceScatter SUM。 |
| [10_ccu_transfer](./10_ccu_transfer/README.md) | 演示本地拷贝、远端写和远端归约写。 |
| [11_allgather_add](./11_allgather_add/README.md) | 先执行CCU AllGather，再对聚合结果执行AICore Add计算。 |
| [12_add_allgather](./12_add_allgather/README.md) | 先执行AICore Add计算，再通过CCU AllGather同步计算结果。 |

## 使用说明

- 样例支持Ascend 950PR&950DT系列产品，CANN版本要求为9.1.0或以上。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 样例会使用环境中可见的全部NPU设备，设备数量不能超过`CCU_MAX_RANK_SIZE`。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
