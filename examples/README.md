# asc-comm样例

本目录提供asc-comm API的使用样例。

## 样例列表

| 样例 | 说明 | 支持产品 |
| --- | --- | --- |
| [ain/basic_ring](./ain/basic_ring/README.md) | 演示多卡环形场景下AIV Kernel通过AIN接口调用`Put`/`Get`进行点对点单边通信，并通过`AinBarrierSession`完成同步与结果校验。 | Ascend 950PR/Ascend 950DT |
| [hcomm_batch_write](./hcomm_batch_write/README.md) | 演示多个URMA Channel共享Jetty，并通过`MakeBatchHandle`、`GetHandleRef`、`BatchCommit`和`Drain`批量提交跨peer写任务。 | Ascend 950PR/Ascend 950DT |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README.md) | 演示多卡场景下AIV Kernel通过URMA路径调用`Hcomm::WriteNbi`和`Hcomm::ReadNbi`，并校验通信结果。 | Ascend 950PR/Ascend 950DT |
| [one_multi_path](./one_multi_path/README.md) | 查询`UB_MEM`链路，为每个peer创建one path/multi path Channel，逐个处理peer，并通过双Stream并发搬运和校验该peer的远端数据。 | Ascend 950PR/Ascend 950DT |
| [simt_notify_atomic](./simt_notify_atomic/README.md) | 演示并验证 SIMT URMA `WriteWithNotifyNbi`、`AtomicFAA` 和 `AtomicCAS` 的单次、批量及多 lane 提交。 | Ascend 950PR / Ascend 950DT |
| [simt_notify_atomic_perf](./simt_notify_atomic_perf/README.md) | 测量SIMT URMA `WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`的下发时延与完成带宽。 | Ascend 950PR / Ascend 950DT |

## simt_notify_atomic

`simt_notify_atomic`验证SIMT Kernel通过URMA路径调用`WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`。样例包含三个单接口用例，以及串行提交、batch-last和多lane批量提交场景。

编译运行方式参见[simt_notify_atomic/README.md](./simt_notify_atomic/README.md)。

性能测试及结果汇总方式参见[simt_notify_atomic_perf/README.md](./simt_notify_atomic_perf/README.md)。

## 运行约束

- 样例支持Ascend 950PR/Ascend 950DT，CANN软件版本要求为9.1.0或以上。
- 样例运行需要至少2张NPU；单卡环境仅支持编译验证。
- 样例编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
