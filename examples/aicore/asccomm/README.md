# asccomm样例

简体中文 | [English](./README_en.md)

本目录提供asccomm点对点通信样例，覆盖AIV直驱URMA、SIMT URMA路径的普通接口、批量接口，以及功能验证与性能测试。

## 样例列表

| 样例 | 说明 |
| --- | --- |
| [01_asccomm_urma_write_read_nbi](./01_asccomm_urma_write_read_nbi/README.md) | 演示多卡场景下AIV Kernel通过URMA路径调用`asccomm::asccomm_write_nbi`和`asccomm::asccomm_read_nbi`，并校验通信结果。 |
| [02_asccomm_urma_batch_write](./02_asccomm_urma_batch_write/README.md) | 演示多个URMA Channel共享Jetty，并通过`asccomm_make_batch_handle`、`asccomm_get_handle_ref`、`asccomm_batch_commit`和`asccomm_drain`批量提交跨peer写任务。 |
| [03_asccomm_urma_one_multi_path](./03_asccomm_urma_one_multi_path/README.md) | 查询`UB_MEM`链路，为每个peer创建one path/multi path Channel，逐个处理peer，并通过双Stream并发搬运和校验该peer的远端数据。 |
| [04_simt_asccomm_urma](./04_simt_asccomm_urma/README.md) | 演示并验证全部 SIMT URMA 接口：`asccomm_write_nbi`、`asccomm_write_value_nbi`、`asccomm_write_with_notify_nbi`、`asccomm_atomic_faa_nbi` 和 `asccomm_atomic_cas_nbi`，覆盖立即提交与批量提交。 |
| [05_simt_asccomm_urma_perftest](./05_simt_asccomm_urma_perftest/README.md) | 测量上述五个 SIMT URMA 接口的下发时延与完成带宽，覆盖立即提交/延迟提交两种发布策略。 |
| [06_simt_asccomm_urma_batch_perf](./06_simt_asccomm_urma_batch_perf/README.md) | 验证六类SIMT URMA接口，对比单lane Batch与Group Batch的下发耗时。 |
| [12_asccomm_urma_normal_api_perf](./12_asccomm_urma_normal_api_perf/README.md) | URMA普通接口性能测试：测量`asccomm_write_nbi`/`asccomm_read_nbi`/`asccomm_write_with_notify_nbi`的时延与带宽，支持p2p/ring拓扑与inline/final两种提交方式。 |
| [13_asccomm_urma_batch_api_perf](./13_asccomm_urma_batch_api_perf/README.md) | URMA批量接口性能测试：按`(len, batch_cap)`分档测量批量下发、`asccomm_batch_commit`与`asccomm_drain`的时延与带宽，支持p2p/ring拓扑与CQE策略调优。 |

## 使用说明

- 样例支持Ascend 950PR&950DT系列产品，具体CANN版本要求以各子样例README为准。
- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 样例编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `asccomm`库。

各样例的编译命令、执行结果和注意事项参见对应目录下的README。
