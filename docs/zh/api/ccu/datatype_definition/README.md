# 简介

本节提供CCU接口使用的数据类型定义，包括状态码、资源句柄与编排上下文参数三类：

- **状态码**：[`ccu_result`](ccu_result.md)，CCU接口的统一状态码类型，表示接口调用或Kernel翻译的执行结果。
- **资源句柄**：6个`uint64_t`句柄类型，标识CCU Kernel内的各类虚拟资源。其中`ccu_variable_handle`与`ccu_event_handle`支持Host侧预约与Kernel内虚拟两种用法，其余4个仅限Kernel内使用。
- **编排上下文参数**：[`ccu_kernel_arg`](ccu_kernel_arg.md)，`<<<>>>`直调Kernel时传递用户自定义参数的`void*`类型。

各句柄类型支持的用法归纳如下：

| 句柄类型 | 标识资源 | Host侧预约用法 | Kernel内虚拟句柄 |
| --- | --- | --- | --- |
| [`ccu_variable_handle`](ccu_variable_handle.md) | `variable`（标量寄存器） | 支持（`asccomm_ccu_variable_reserved`预约、`asccomm_ccu_variable_get_addr`查询地址） | 支持 |
| [`ccu_event_handle`](ccu_event_handle.md) | `event`（事件槽位） | 支持（`asccomm_ccu_event_reserved`预约、`asccomm_ccu_event_get_addr`查询地址） | 支持 |
| [`ccu_address_handle`](ccu_address_handle.md) | `address`（地址寄存器） | 不支持 | 支持 |
| [`ccu_buffer_handle`](ccu_buffer_handle.md) | `ccu_buffer`（片上暂存切片） | 不支持 | 支持 |
| [`ccu_local_addr_handle`](ccu_local_addr_handle.md) | `local_addr`（本端地址复合资源） | 不支持 | 支持 |
| [`ccu_remote_addr_handle`](ccu_remote_addr_handle.md) | `remote_addr`（对端地址复合资源） | 不支持 | 支持 |

## 接口列表

- [ccu_result](ccu_result.md)
- [ccu_variable_handle](ccu_variable_handle.md)
- [ccu_event_handle](ccu_event_handle.md)
- [ccu_address_handle](ccu_address_handle.md)
- [ccu_buffer_handle](ccu_buffer_handle.md)
- [ccu_local_addr_handle](ccu_local_addr_handle.md)
- [ccu_remote_addr_handle](ccu_remote_addr_handle.md)
- [ccu_kernel_arg](ccu_kernel_arg.md)
