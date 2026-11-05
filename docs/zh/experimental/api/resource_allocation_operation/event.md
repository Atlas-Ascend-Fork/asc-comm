# 事件

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

`__ccu_event_t`是kernel内的本地完成事件。异步搬运完成后，硬件设置指定mask；调用
`__ccu_event_wait`等待。

事件的置位、清除和等待见[同步接口](../synchronization/README.md)。

## 调用示例

```cpp
__ccu_event_t done;
__ccu_local_copy_nb(output, input, bytes, done, 1);
__ccu_event_wait(done, 1);
```
