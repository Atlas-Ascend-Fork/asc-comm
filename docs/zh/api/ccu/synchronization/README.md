# 简介

本节提供CCU kernel内的同步接口：数据搬运等异步操作之间需要建立先后依赖时，生产侧通过调用record接口写入完成信号，消费侧调用wait接口阻塞等待信号到达。按信号的作用范围分为两类：

| 类型 | 适用场景 | 接口 |
| --- | --- | --- |
| 本地event | 同一kernel内等待异步搬运完成 | [event_record](event_record.md)、[event_wait](event_wait.md) |
| 远端notify | 跨die（含跨device、跨节点）通过channel向对端收发信号 | [notify_record](notify_record.md)、[notify_wait](notify_wait.md)、[write_variable_with_notify](write_variable_with_notify.md) |

[write_variable_with_notify](write_variable_with_notify.md)将“写远端variable值”与“触发远端notify”合并为原子操作，用于需要将标量值连同完成信号一起发送给对端的场景。

## 接口列表

- [event_record](event_record.md)
- [event_wait](event_wait.md)
- [notify_record](notify_record.md)
- [notify_wait](notify_wait.md)
- [write_variable_with_notify](write_variable_with_notify.md)
