# 预留接口

本章节列出的接口均为预留接口，后续可能存在变更，暂不推荐开发者使用。

## CCU API

### 资源创建与操作

| 接口名 | 描述 |
| ---- | ---- |
| asccomm_ccu_host_kernel_launch | Host侧下发CCU kernel：根据kernel函数指针与名称匹配或注册kernel句柄，并按launch配置（mission个数、die掩码、CCU实例、stream等）将kernel任务下发到指定stream上执行。 |
| HcommCcuHostKernelLaunch | asccomm_ccu_host_kernel_launch的兼容入口：接收hcomm旧调用方的驼峰风格配置HcommLaunchKernelCfg，逐字段等价转换为asccomm_launch_kernel_cfg后调用asccomm_ccu_host_kernel_launch。 |
| HcommCcuGetLaunchHashTag | asccomm_ccu_get_launch_hash_tag的兼容入口：根据tag字符串计算64位哈希值，作为launch配置中的二进制缓存标签使用；tag为空指针时返回0。 |
| asccomm_ccu_variable_reserved | 在指定CCU实例上预约一段连续的Variable（标量寄存器）资源，并在预约阶段完成地址映射，返回预约句柄。 |
| asccomm_ccu_variable_get_addr | 查询Variable预约句柄名下第index个标量寄存器的进程可访问虚拟地址；地址在预约阶段已映射并缓存，本接口为纯查表，可重复调用。 |
| asccomm_ccu_event_reserved | 在指定CCU实例上预约一段连续的Event（完成事件单元）资源，参数与生命周期约束同asccomm_ccu_variable_reserved。 |
| asccomm_ccu_event_get_addr | 查询Event预约句柄名下第index个完成事件单元的进程可访问虚拟地址，错误码与约束同asccomm_ccu_variable_get_addr。 |
