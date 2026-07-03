# asc-comm样例

本目录提供asc-comm API的使用样例。

| 样例 | 说明 |
| --- | --- |
| [AICore Hcomm点对点通信Kernel样例](./aicore/hcomm_p2p_kernel/README.md) | 展示在Ascend C Kernel中初始化`AscendC::Hcomm`，提交读写任务，并通过`Commit`和`Drain`控制任务执行。 |
| [完整算子集成说明](./aicore/hcomm_p2p_kernel/INTEGRATION.md) | 说明如何将本仓Kernel侧样例与hcomm control-plane、devkit/CANN算子工程组合成完整用例。 |

当前样例聚焦AICore侧API调用方式。完整算子工程还需要结合CANN算子工程、Tiling、Host侧单算子调用，以及HCOMM/HCCL control-plane资源创建流程；这些外部依赖在集成说明中列出。
