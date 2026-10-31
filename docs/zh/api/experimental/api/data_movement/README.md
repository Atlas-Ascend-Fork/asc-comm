# 数据搬运

本节提供CCU kernel内的数据搬运接口，覆盖本地地址、本地CCU Buffer、peer远端地址和本地/远端归约搬运。
搬运接口均为异步接口，完成后设置event的指定mask，消费侧通过同步接口等待完成。

## 接口列表

- [本地拷贝](local_copy.md)
- [远端读写](read_write.md)
- [远端归约写](write_reduce.md)

## 数据通路

| 类型 | 数据通路 |
| --- | --- |
| 本地拷贝 | 本地地址 ↔ 本地地址、CCU Buffer |
| 远端读写 | 本地地址 ↔ peer远端地址 |
| 远端归约写 | 本地数据写入peer，并在目标端执行sum/max/min |
| 远端归约读 | 从peer读取数据，并在本端目标执行sum/max/min |
| 本地归约 | 在本地地址或本端CCU Buffer上执行sum/max/min |
