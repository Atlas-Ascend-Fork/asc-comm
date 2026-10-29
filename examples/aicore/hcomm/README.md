# Hcomm样例

简体中文 | [English](./README_en.md)

本目录提供Hcomm点对点通信样例，覆盖AIV直驱URMA、SIMT URMA和RoCE路径的普通接口、批量接口、多SGE离散读写，以及功能验证与性能测试。

## 样例列表

| 样例 | 说明 |
| --- | --- |
| [01_hcomm_urma_write_read_nbi](./01_hcomm_urma_write_read_nbi/README.md) | 演示多卡场景下AIV Kernel通过URMA路径调用`Hcomm::WriteNbi`和`Hcomm::ReadNbi`，并校验通信结果。 |
| [02_hcomm_urma_batch_write](./02_hcomm_urma_batch_write/README.md) | 演示多个URMA Channel共享Jetty，并通过`MakeBatchHandle`、`GetHandleRef`、`BatchCommit`和`Drain`批量提交跨peer写任务。 |
| [03_hcomm_urma_one_multi_path](./03_hcomm_urma_one_multi_path/README.md) | 查询`UB_MEM`链路，为每个peer创建one path/multi path Channel，逐个处理peer，并通过双Stream并发搬运和校验该peer的远端数据。 |
| [04_simt_hcomm_urma](./04_simt_hcomm_urma/README.md) | 演示并验证全部 SIMT URMA 接口：`WriteNbi`、`WriteValueNbi`、`WriteWithNotifyNbi`、`AtomicFAA` 和 `AtomicCAS`，覆盖立即提交与批量提交。 |
| [05_simt_hcomm_urma_perftest](./05_simt_hcomm_urma_perftest/README.md) | 测量上述五个 SIMT URMA 接口的下发时延与完成带宽，覆盖立即提交/延迟提交两种发布策略。 |
| [06_simt_hcomm_urma_batch_perf](./06_simt_hcomm_urma_batch_perf/README.md) | 验证六类SIMT URMA接口，对比单lane Batch与Group Batch的下发耗时。 |
| [07_hcomm_roce_normal_write_read](./07_hcomm_roce_normal_write_read/README.md) | 演示MPI多进程场景下AIV Kernel通过RoCE路径调用普通`WriteNbi`/`ReadNbi`：延迟提交后由`Commit`统一触发，`Drain`等待完成并校验结果。 |
| [08_hcomm_roce_batch_write_read](./08_hcomm_roce_batch_write_read/README.md) | 演示MPI多进程场景下RoCE路径批量接口：通过`MakeBatchHandle`在批量句柄上连续提交`WriteNbi`/`ReadNbi`，`BatchCommit`统一触发并校验环形拓扑读写结果。 |
| [09_hcomm_roce_multi_sge](./09_hcomm_roce_multi_sge/README.md) | 演示MPI多进程场景下RoCE批量多SGE接口：`WriteNbi`/`ReadNbi`单次携带多段离散SGE，分组提交并校验离散读写结果。 |

## 使用说明

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 样例编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
- RoCE样例（07-09）使用MPI管理多进程，需要先配置MPI环境变量并通过`mpirun`启动，具体以各子样例README为准。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
