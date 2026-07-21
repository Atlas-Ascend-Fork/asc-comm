# asc-comm 样例

本目录提供 asc-comm API 的使用样例。

## 样例列表

| 样例 | 说明 | 支持产品 |
| --- | --- | --- |
| [custom_ccu_allgather](./custom_ccu_allgather/README.md) | 演示基于 HCCL 通信接口和 CCU_SCHED 通信引擎实现 AllGather 集合通信操作。 | Ascend 950PR/Ascend 950DT |
| [custom_ccu_allreduce](./custom_ccu_allreduce/README.md) | 演示基于 HCCL 通信接口和 CCU_SCHED 通信引擎实现 AllReduce SUM 集合通信操作。 | Ascend 950PR/Ascend 950DT |
| [custom_ccu_reduce_scatter](./custom_ccu_reduce_scatter/README.md) | 演示基于 CCU 数据面 kernel 实现 ReduceScatter SUM 集合通信操作。 | Ascend 950PR/Ascend 950DT |
| [custom_ccu_all_to_all](./custom_ccu_all_to_all/README.md) | 演示基于 CCU 数据面 kernel 实现等长 AllToAll 集合通信操作。 | Ascend 950PR/Ascend 950DT |
