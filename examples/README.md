# asc-comm样例

本目录提供asc-comm API的使用样例。

## 样例列表

| 样例 | 说明 | 支持产品 |
| --- | --- | --- |
| [ain/basic_ring](./ain/basic_ring/README.md) | 演示多卡环形场景下AIV Kernel通过AIN接口调用`Put`/`Get`进行点对点单边通信，并通过`AinBarrierSession`完成同步与结果校验。 | Ascend 950PR/Ascend 950DT |
| [hcomm_batch_write](./hcomm_batch_write/README.md) | 演示多个URMA Channel共享Jetty，并通过`MakeBatchHandle`、`GetHandleRef`、`BatchCommit`和`Drain`批量提交跨peer写任务。 | Ascend 950PR/Ascend 950DT |
| [hcomm_handle_write_perf](./hcomm_handle_write_perf/README.md) | 直接使用DeepEP `handle.hpp`中的`HcommPeer`和`HcommJetty`接口，测量连续提交1000次256字节WRITE的平均设备周期耗时，并完成结果校验。 | Ascend 950PR/Ascend 950DT |
| [hcomm_jetty_write_perf](./hcomm_jetty_write_perf/README.md) | 测量AIV Kernel通过URMA Jetty `HcommJetty::Write`连续提交1000次的平均设备周期耗时，最终通过`Drain`完成并校验写入结果。 | Ascend 950PR/Ascend 950DT |
| [hcomm_write_read_nbi](./hcomm_write_read_nbi/README.md) | 演示多卡场景下AIV Kernel通过URMA路径调用`Hcomm::WriteNbi`和`Hcomm::ReadNbi`，并校验通信结果。 | Ascend 950PR/Ascend 950DT |
| [one_multi_path](./one_multi_path/README.md) | 查询`UB_MEM`链路，为每个peer创建one path/multi path Channel，逐个处理peer，并通过双Stream并发搬运和校验该peer的远端数据。 | Ascend 950PR/Ascend 950DT |
| [simt_urma](./simt_urma/README.md) | 演示并验证全部 SIMT URMA 接口：`WriteNbi`、`WriteValueNbi`、`WriteWithNotifyNbi`、`AtomicFAA` 和 `AtomicCAS`，覆盖立即提交与批量提交。 | Ascend 950PR / Ascend 950DT |
| [simt_urma_perftest](./simt_urma_perftest/README.md) | 测量上述五个 SIMT URMA 接口的下发时延与完成带宽，覆盖立即提交/延迟提交两种发布策略。 | Ascend 950PR / Ascend 950DT |

## simt_urma

`simt_urma`验证SIMT Kernel通过URMA路径调用`WriteNbi`、`WriteValueNbi`、`WriteWithNotifyNbi`、
`AtomicFAA`和`AtomicCAS`。样例包含单接口用例，以及串行提交和batch-last提交场景，共十种模式。

编译运行方式参见[simt_urma/README.md](./simt_urma/README.md)。

## hcomm_jetty_write_perf

`hcomm_jetty_write_perf`对每个peer连续提交1000次Jetty `Put`，输出只包围提交循环的平均设备周期耗时，
并在计时循环之后调用`Drain`完成通信和校验。

性能测试方式参见[hcomm_jetty_write_perf/README.md](./hcomm_jetty_write_perf/README.md)。

## hcomm_handle_write_perf

`hcomm_handle_write_perf`完整内置DeepEP `handle.hpp`及其直接依赖，使用`HcommPeer`和`HcommJetty`接口对每个peer连续提交1000次256字节WRITE，输出只包围提交循环的平均设备周期耗时，并在计时循环之后调用`drain`完成通信和校验。

性能测试方式参见[hcomm_handle_write_perf/README.md](./hcomm_handle_write_perf/README.md)。

## simt_urma_perftest

`simt_urma_perftest`测量上述五个接口的性能，分别统计下发时延与完成带宽，并对`write`和`notify`
支持payload扫描。

性能测试方式参见[simt_urma_perftest/README.md](./simt_urma_perftest/README.md)。

## 运行约束

- 样例支持Ascend 950PR/Ascend 950DT，CANN软件版本要求为9.1.0或以上。
- 样例运行需要至少2张NPU；单卡环境仅支持编译验证。
- 样例编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
