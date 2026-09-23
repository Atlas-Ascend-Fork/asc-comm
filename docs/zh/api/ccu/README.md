# CCU接口简介

CCU编程API分为控制面和数据面两类：控制面接口在Host侧调用，负责建链、资源预约、Kernel注册与下发等准备工作，接口详细说明参见[hcomm控制面接口](https://gitcode.com/cann/hcomm/tree/master/docs/zh/api_ref/comm_opdev/control_plane_api)；数据面接口在Kernel函数体内调用，负责描述数据搬运、同步等通信逻辑，是本章的介绍内容。一次CCU通信任务由两类接口配合完成：控制面将准备好的链路和资源传入Kernel，数据面描述的通信逻辑经注册翻译后由CCU硬件执行。

## 数据面接口的执行机制

数据面接口的生命周期分为注册与执行两个阶段：

- **注册阶段**：`<<<>>>`直调时，Kernel函数体被执行一次，其中的数据面接口调用不触发硬件操作，仅被逐条翻译为CCU指令。
- **执行阶段**：`<<<>>>`下发任务后，CCU硬件执行翻译出的指令序列，完成实际的数据搬运与同步。

## 数据面接口分类

CCU数据面接口按功能分为以下几类：

- [资源创建与操作](./resource_allocation_operation/README.md)
- [参数加载/存储](./arg_load_store/README.md)
- [数据搬运](./data_movement/README.md)
- [同步](./synchronization/README.md)
- [流程控制](./execution_control/README.md)
