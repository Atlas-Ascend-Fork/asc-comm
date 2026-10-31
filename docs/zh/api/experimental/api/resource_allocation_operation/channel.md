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

编译器通过以下数组提供逻辑peer channel和channel关联的变量资源槽位：

```cpp
__ccu_channels[channel_index]
__ccu_channel_xns[channel_index][xn_index]
```

channel index和变量资源槽位index必须是编译期常量。`channel_index`的有效范围由
`rankSize - 1`决定。当前public数组的peer-visible容量为126；`CCU_CHANNEL_MAX`表示目标平台总channel上限。常用槽位含义：`[1]`为远端地址，`[2]`为对应token。

## 约束说明

- kernel不接收底层channel ID。
- channel关联的变量资源只用于需要peer交换地址/token或变量的场景。
- 实际channel和关联变量资源由运行时根据通信域完成解析。
- 显式传入channel时，`channelSize`必须覆盖kernel的peer channel数量；未显式传入时由部署环境提供。

## 调用示例

```cpp
__ccu_event_t done;
__ccu_addr_t remote = __ccu_addr(__ccu_channel_xns[0][1],
                                 __ccu_channel_xns[0][2]);
__ccu_write_nb(__ccu_channels[0], remote, local_src, bytes, done, 1);
__ccu_event_wait(done, 1);
```
