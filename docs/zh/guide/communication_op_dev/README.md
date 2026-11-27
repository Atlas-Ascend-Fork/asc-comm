# 通信算子开发

本目录提供基于各通信引擎API的通信算子开发指导，覆盖AI Core侧直驱通信、AI CPU侧代理通信和CCU通算融合通信场景。开发前建议先了解[编程模型](../programming_model/README.md)。

| 章节 | 说明 |
| --- | --- |
| [基于CommRW的AIV直驱通信算子开发](./aiv_asccomm_operator_impl.md) | 介绍基于AI Core侧CommRW点对点通信接口的通信算子开发流程，覆盖Host侧控制面准备与Kernel侧数据面执行。 |
| [基于SIMT CommRW的AIV直驱通信算子开发](./aiv_asccomm_simt_operator_impl.md) | 介绍基于AI Core侧SIMT CommRW点对点通信接口的通信算子开发流程，适用于SIMT执行空间下的通信算子。 |
| [基于AIN的通信算子开发](./ain_operator_impl.md) | 介绍基于AI Core侧AIN单边通信接口的通信算子开发流程。 |
| [基于MC2的通算算子开发](./mc2_operator_impl.md) | 介绍基于MC2代理模式的通算算子开发流程。 |
| [CCU通信算子开发](./ccu_comm_op_dev.md) | 介绍开发CCU通信算子的全流程，覆盖Host侧资源准备、任务下发与Kernel侧通信执行。 |
| [CCU通算融合算子开发](./ccu_comm_compute_op_dev.md) | 介绍开发CCU通算融合算子的全流程，将AI Core上的计算与CCU上的集合通信融合到同一个算子内。 |
