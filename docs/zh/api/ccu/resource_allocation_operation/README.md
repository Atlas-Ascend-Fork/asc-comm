# 简介

本节提供CCU kernel内的资源类，提供对虚拟资源句柄的申请与绑定接口，以及对`variable`、`address`进行赋值与算术运算的接口。资源类是硬件资源的C++封装，编程时操作的是C++对象，底层对应CCU的寄存器或存储单元。

获取资源有以下几种方式：

- **默认构造**：新建一个对象。
- **array批量构造**：一次新建物理连续的一批，满足LoopGroup复制场景对连续资源的要求。
- **预约构造**：绑定Host侧预约的资源。
- **引用channel预留**：复用channel建链时预留的共享槽位。

各资源类支持的获取方式归纳如下：

| 资源类型 | 编程对象 | 获取方式 |
| --- | --- | --- |
| 通用寄存器 | `variable` | 默认构造：[variable()](variable.md)<br>批量构造：[array\<variable\>](array.md)<br>预约构造：[variable(var_handle, index)](variable.md)<br>引用channel预留：[get_res_by_channel](get_res_by_channel.md) |
| 同步寄存器槽位 | `event` | 默认构造：[event()](event.md)<br>批量构造：[array\<event\>](array.md)<br>预约构造[event(acq_handle, index)](event.md) |
| 片上缓存（CCU Buffer，4KB） | `ccu_buffer` | 默认构造：[ccu_buffer()](ccu_buffer.md)<br>批量构造：[array\<ccu_buffer\>](array.md) |
| 地址寄存器 | `address` | 默认构造：[address()](address.md) |
| 本端地址+token | `local_addr` | 默认构造：[local_addr()](local_addr.md) |
| 对端地址+token | `remote_addr` | 默认构造：[remote_addr()](remote_addr.md) |

[variable](variable.md)和[address](address.md)除资源获取外，还提供赋值与算术运算符，详见对应文档。

## 接口列表

- [variable](variable.md)
- [address](address.md)
- [event](event.md)
- [ccu_buffer](ccu_buffer.md)
- [local_addr](local_addr.md)
- [remote_addr](remote_addr.md)
- [array](array.md)
- [get_res_by_channel](get_res_by_channel.md)
