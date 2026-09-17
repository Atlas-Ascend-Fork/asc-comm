# asc-comm 样例

简体中文 | [English](./README_en.md)

本目录提供asc-comm API的使用样例。

## 样例列表

| 样例 | 说明 | 支持产品 |
| --- | --- | --- |
| [aicore/ain/01_basic_ring](./aicore/ain/01_basic_ring/README.md) | 演示多卡环形场景下AIV Kernel通过AIN接口调用`Put`/`Get`进行点对点单边通信，并通过`AinBarrierSession`完成同步与结果校验。 | Ascend 950PR/Ascend 950DT |
| [aicore/hcomm/01_hcomm_write_read_nbi](./aicore/hcomm/01_hcomm_write_read_nbi/README.md) | 演示多卡场景下AIV Kernel通过URMA路径调用`Hcomm::WriteNbi`和`Hcomm::ReadNbi`，并校验通信结果。 | Ascend 950PR/Ascend 950DT |
| [aicore/hcomm/02_hcomm_batch_write](./aicore/hcomm/02_hcomm_batch_write/README.md) | 演示多个URMA Channel共享Jetty，并通过`MakeBatchHandle`、`GetHandleRef`、`BatchCommit`和`Drain`批量提交跨peer写任务。 | Ascend 950PR/Ascend 950DT |
| [aicore/hcomm/03_one_multi_path](./aicore/hcomm/03_one_multi_path/README.md) | 查询`UB_MEM`链路，为每个peer创建one path/multi path Channel，逐个处理peer，并通过双Stream并发搬运和校验该peer的远端数据。 | Ascend 950PR/Ascend 950DT |
| [aicore/hcomm/04_simt_urma](./aicore/hcomm/04_simt_urma/README.md) | 演示并验证全部 SIMT URMA 接口：`WriteNbi`、`WriteValueNbi`、`WriteWithNotifyNbi`、`AtomicFAA` 和 `AtomicCAS`，覆盖立即提交与批量提交。 | Ascend 950PR / Ascend 950DT |
| [aicore/hcomm/05_simt_urma_perftest](./aicore/hcomm/05_simt_urma_perftest/README.md) | 测量上述五个 SIMT URMA 接口的下发时延与完成带宽，覆盖立即提交/延迟提交两种发布策略。 | Ascend 950PR / Ascend 950DT |
| [hcomm_jetty_write](./hcomm_jetty_write/README.md) | 演示多卡场景下AIV Kernel通过`HcommJetty::Write`和`HcommJetty::WriteValue`直接向Jetty SQ提交WQE，并通过`Drain`等待完成与校验结果。 | Ascend 950PR/Ascend 950DT |
| [ccu/ccu_direct](./ccu/ccu_direct/01_allgather/README.md) | 演示基于HCCL通信域和CCU数据面接口，以直调`<<<>>>`方式实现AllGather等集合通信操作。 | Ascend 950PR/Ascend 950DT |

各样例的编译与运行方式参见对应目录下的README。

## 运行约束

- 样例支持Ascend 950PR/Ascend 950DT，CANN软件版本要求为9.1.0或以上。
- 样例运行需要至少2张NPU；单卡环境仅支持编译验证。
- 样例编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
