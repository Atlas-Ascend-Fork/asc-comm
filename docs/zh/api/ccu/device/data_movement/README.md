# 简介

本节提供CCU kernel内的数据搬运接口，覆盖本地数据搬运和远端数据搬运，远端数据搬运可选随路归约。

所有数据搬运接口均为异步接口，硬件完成搬运时自动将`event`中`mask`对应的标志位设置为1，下游通过`event_wait`等待完成信号。按数据通路分为以下两类：

| 类型 | 适用场景 | 接口 |
| --- | --- | --- |
| 本地操作 | 本端片上内存↔本端片上内存或本端片上内存↔本端CCU Buffer之间的拷贝与归约 | [local_copy](local_copy.md)、[local_reduce](local_reduce.md) |
| 跨rank操作 | 通过channel在本端与对端片上内存之间读写数据，可选归约 | [read](read.md)、[read_reduce](read_reduce.md)、[write](write.md)、[write_reduce](write_reduce.md) |

跨rank操作要求所有`ChannelHandle`属于同一die，由框架统一校验。

## 接口列表

- [local_copy](local_copy.md)
- [local_reduce](local_reduce.md)
- [read](read.md)
- [read_reduce](read_reduce.md)
- [write](write.md)
- [write_reduce](write_reduce.md)
