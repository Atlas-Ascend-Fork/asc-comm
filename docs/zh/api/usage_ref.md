# 使用参考

本章节提供Hcomm通信接口的使用说明及相关样例代码，帮助开发者快速掌握AI Core侧点对点通信与AIN单边通信的调用方式。

## Hcomm通信接口使用说明

[Hcomm通信接口使用说明](../guide/hcomm_usage.md)：介绍AI Core侧Hcomm点对点通信接口的使用方法，涵盖普通接口流程（Init → 提交任务 → Drain）、单通道BatchHandle批量接口流程、共享Jetty BatchHandle多通道流程、批量缓冲区与CQE配置、协议差异、多AI Core共享通道的Lock/Unlock机制及注意事项。

## 样例代码

- [AIV直驱URMA WriteNbi/ReadNbi样例](../../../examples/aicore/hcomm/01_hcomm_write_read_nbi/README.md)：展示如何在AIV Kernel中通过Hcomm的WriteNbi/ReadNbi接口实现NPU间低时延点对点通信，包含Host侧通信域创建、通道建立及Kernel侧Init → WriteNbi/ReadNbi → Drain的完整流程，支持Ascend 950PR/Ascend 950DT多卡环形拓扑。
- [Ain Basic Ring样例](../../../examples/aicore/ain/01_basic_ring/README.md)：展示如何通过AIN接口在AIV Kernel中实现多rank环形通信，包括Put写入对端window、Get读取对端window、Flush等待通信完成及AinBarrierSession::Sync同步，支持Ascend 950PR/Ascend 950DT单机多卡场景。
