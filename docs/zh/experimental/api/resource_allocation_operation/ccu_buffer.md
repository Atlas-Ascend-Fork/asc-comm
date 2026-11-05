# CCU缓冲区

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

`__ccu_buf_block_t`表示本地CCU缓冲区块，用于通信地址和本地缓冲区之间的数据搬运以及本地缓冲区归约。

## 相关接口

- [本地拷贝](../data_movement/local_copy.md)
- [本地CCU缓冲区归约](../data_movement/local_copy.md)
- [远端归约写](../data_movement/write_reduce.md)

## 约束说明

- 缓冲区块容量不超过目标设备支持的单块容量，且缓冲区访问满足目标设备对齐要求。
- 只有在异步搬运完成后，才能读取目标缓冲区。

## 调用示例

```cpp
__ccu_buf_block_t staging;
__ccu_event_t done;
__ccu_local_copy_to_buf_nb(staging, input, bytes, done, 1);
__ccu_event_wait(done, 1);
__ccu_local_copy_from_buf_nb(output, staging, bytes, done, 1);
__ccu_event_wait(done, 1);
```
