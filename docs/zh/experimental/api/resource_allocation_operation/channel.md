# Channel

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

编译器提供以下资源数组，分别表示逻辑对端channel和channel关联的变量资源槽位：

```cpp
__ccu_channels[channel_index]
__ccu_channel_xns[channel_index][xn_index]
```

channel下标和变量资源槽位下标必须是编译期常量。`channel_index`的有效范围由
`rankSize - 1`决定。`__ccu_channels`数组大小为126；`CCU_CHANNEL_MAX`表示平台总channel上限。
`xn_index`的有效取值为0~3。

## 约束说明

- 变量资源槽位含义由通信双方约定。
- 显式传入channel时，`channelList`为channel句柄数组，`channelSize`为数组元素个数。
- 未显式传入channel时，`channelSize`填`0`，`channelList`填空指针。

## 调用示例

```cpp
__ccu_event_t done;
__ccu_addr_t remote = __ccu_addr(__ccu_channel_xns[0][1],
                                 __ccu_channel_xns[0][2]);
__ccu_write_nb(__ccu_channels[0], remote, local_src, bytes, done, 1);
__ccu_event_wait(done, 1);
```
