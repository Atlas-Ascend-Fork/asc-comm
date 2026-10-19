# CCU Custom Kernel样例

简体中文 | [English](./README_en.md)

本目录提供自定义CCU Kernel样例。样例基于HCCL通信域申请CCU线程、Channel、实例和内存Token，并通过`<<<>>>`直调或`asccomm_ccu_kernel_launch`下发任务。

## 样例列表

| 样例 | 说明 |
| --- | --- |
| [01_allgather](./01_allgather/README.md) | 演示以`<<<>>>`直调方式执行CCU AllGather Kernel。 |
| [02_allgather_mesh1d_mem2mem](./02_allgather_mesh1d_mem2mem/README.md) | 演示以`<<<>>>`直调方式执行Mesh1D Mem2Mem AllGather数据面。 |
| [03_allgather_nhr1d_mem2mem](./03_allgather_nhr1d_mem2mem/README.md) | 演示以`<<<>>>`直调方式执行NHR1D Mem2Mem AllGather数据面。 |
| [04_allreduce](./04_allreduce/README.md) | 演示以`<<<>>>`直调方式执行AllReduce SUM Kernel。 |
| [05_all_to_all](./05_all_to_all/README.md) | 演示以`<<<>>>`直调方式执行等长AllToAll数据交换。 |
| [06_all_to_allv](./06_all_to_allv/README.md) | 演示以`<<<>>>`直调方式执行可变长度AllToAllV数据交换。 |
| [07_all_to_all_vc](./07_all_to_all_vc/README.md) | 演示以`<<<>>>`直调方式执行基于count matrix的AllToAllVC数据交换。 |
| [08_broadcast](./08_broadcast/README.md) | 演示以`<<<>>>`直调方式执行Broadcast数据广播。 |
| [09_reduce_scatter](./09_reduce_scatter/README.md) | 演示以`<<<>>>`直调方式执行ReduceScatter SUM。 |
| [10_ccu_transfer](./10_ccu_transfer/README.md) | 演示以`<<<>>>`直调方式执行本地拷贝、远端写和远端归约写三类基础搬运接口。 |

## 使用说明

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本和规模约束以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前通常需要设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`，以子样例README为准。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
