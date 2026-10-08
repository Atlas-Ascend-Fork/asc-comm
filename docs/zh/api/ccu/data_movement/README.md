# 简介

本节提供CCU kernel内的数据搬运接口，覆盖本地数据搬运和远端数据搬运，远端数据搬运可选随路归约。

所有数据搬运接口均为异步接口，硬件完成搬运时自动将`event`中`mask`对应的标志位设置为1，下游通过`event_wait`等待完成信号。按数据通路分为以下两类：

| 类型 | 适用场景 | 接口 |
| --- | --- | --- |
| 本地数据搬运 | 本端片上内存↔本端片上内存、本端片上内存↔本端CCU Buffer之间的拷贝 | [local_copy](local_copy.md) |
| 远端数据搬运 | 通过channel将本端数据写入对端片上内存，可选归约 | [write](write.md)、[write_reduce](write_reduce.md) |

## 接口列表

- [local_copy](local_copy.md)
- [write](write.md)
- [write_reduce](write_reduce.md)
