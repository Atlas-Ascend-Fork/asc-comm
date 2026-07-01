# asc-comm 样例

本目录提供 asc-comm API 的使用样例。

| 样例 | 说明 |
| --- | --- |
| [AICore Hcomm 点对点通信 Kernel 样例](./aicore/hcomm_p2p_kernel/README.md) | 展示在 Ascend C Kernel 中初始化 `AscendC::Hcomm`，提交读写任务，并通过 `Commit` 和 `Drain` 控制任务执行。 |
| [完整算子集成说明](./aicore/hcomm_p2p_kernel/INTEGRATION.md) | 说明如何将本仓 Kernel 侧样例与 hcomm control-plane、devkit/CANN 算子工程组合成完整用例。 |

当前样例聚焦 AICore 侧 API 调用方式。完整算子工程还需要结合 CANN 算子工程、Tiling、Host 侧单算子调用，以及 HCOMM/HCCL control-plane 资源创建流程；这些外部依赖在集成说明中列出。
