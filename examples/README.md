# asc-comm 样例

本目录提供 asc-comm API 的使用样例。

## 样例列表

| 样例 | 说明 | 支持产品 |
| --- | --- | --- |
| [custom_ccu_allgather](./custom_ccu_allgather/README.md) | 演示基于 HCCL 通信接口和 CCU_SCHED 通信引擎实现 AllGather 集合通信操作。 | Ascend 950PR/Ascend 950DT |
| [custom_ccu_allreduce](./custom_ccu_allreduce/README.md) | 演示基于 HCCL 通信接口和 CCU_SCHED 通信引擎实现 AllReduce SUM 集合通信操作。 | Ascend 950PR/Ascend 950DT |
| [custom_ccu_reduce_scatter](./custom_ccu_reduce_scatter/README.md) | 演示基于 CCU 数据面 kernel 实现 ReduceScatter SUM 集合通信操作。 | Ascend 950PR/Ascend 950DT |
| [custom_ccu_all_to_all](./custom_ccu_all_to_all/README.md) | 演示基于 CCU 数据面 kernel 实现等长 AllToAll 集合通信操作。 | Ascend 950PR/Ascend 950DT |
| [ccu_direct/01_allgather](./ccu_direct/01_allgather/README.md) | 演示如何基于 HCCL 通信域和 CCU 数据面接口，并以直调第一阶段方式实现 AllGather 集合通信操作。 | Ascend 950PR/Ascend 950DT |
| [ccu_direct/02_allgather_add](./ccu_direct/02_allgather_add/README.md) | 演示如何先以直调第一阶段方式通过 CCU 完成 AllGather 通信，再通过 AICore vector kernel 对 AllGather 结果执行 AICore Add 计算。 | Ascend 950PR/Ascend 950DT |
| [ccu_direct/03_add_allgather](./ccu_direct/03_add_allgather/README.md) | 演示先通过 AICore vector kernel 执行 AICore Add 计算，再以计算结果作为输入以直调第一阶段方式执行 CCU AllGather 通信。 | Ascend 950PR/Ascend 950DT |
