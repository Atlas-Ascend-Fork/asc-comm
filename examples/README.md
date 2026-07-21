# asc-comm 样例

本目录提供 asc-comm API 的使用样例。

## 样例列表

| 样例 | 说明 | 支持产品 |
| --- | --- | --- |
| [ain/basic_ring](./ain/basic_ring/README.md) | 演示多卡环形场景下AIV Kernel通过AIN接口调用`Put`/`Get`进行点对点单边通信，并通过`AinBarrierSession`完成同步与结果校验。 | Ascend 950PR/Ascend 950DT |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README.md) | 演示多卡场景下AIV Kernel通过URMA路径调用`Hcomm::WriteNbi`和`Hcomm::ReadNbi`，并校验通信结果。 | Ascend 950PR/Ascend 950DT |
| [one_multi_path](./one_multi_path/README.md) | 查询`UB_MEM`链路，为每个peer创建one path/multi path Channel，逐个处理peer，并通过双Stream并发搬运和校验该peer的远端数据。 | Ascend 950PR/Ascend 950DT |
| [custom_ccu_allgather](./custom_ccu_allgather/README.md) | 演示基于 HCCL 通信接口和 CCU_SCHED 通信引擎实现 AllGather 集合通信操作。 | Ascend 950PR/Ascend 950DT |
| [custom_ccu_allreduce](./custom_ccu_allreduce/README.md) | 演示基于 HCCL 通信接口和 CCU_SCHED 通信引擎实现 AllReduce SUM 集合通信操作。 | Ascend 950PR/Ascend 950DT |
