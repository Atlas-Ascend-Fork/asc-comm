# 资源申请与操作

CCU kernel使用编译器管理的逻辑资源句柄。Host负责准备通信域、channel、buffer和token；
kernel只声明并使用逻辑variable、event、address、channel和CCU Buffer，不填写底层资源ID。

## 资源列表

| 资源 | 文档 |
| --- | --- |
| 地址和地址偏移 | [address](address.md) |
| Variable | [variable](variable.md) |
| Event | [event](event.md) |
| CCU Buffer | [ccu_buffer](ccu_buffer.md) |
| Channel和channel关联变量资源 | [channel](channel.md) |

## 获取原则

- kernel内声明的资源由编译器统一分配。
- 不要从地址或buffer起始值反推资源编号。
