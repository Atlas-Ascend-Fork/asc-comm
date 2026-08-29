# Lock

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持URMA协议，不支持RoCE协议
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品AI Core：不支持
- Atlas 推理系列产品Vector Core：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：不支持
<!-- end id6 -->
<!-- npu="x90" id7 -->
- Kirin X90：不支持
<!-- end id7 -->

## 功能说明

多个AI Core共享同一通信通道时，获取该通道的跨AI Core锁。`channel`有效时，如果锁已被持有，则等待直至成功获取锁，不会因锁竞争返回失败；不保证多个AI Core间的锁获取顺序。

## 函数原型

```cpp
__aicore__ inline int32_t Lock(AscendC::ChannelHandle channel);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 获取锁成功。 |
| `-1` | `channel`为空或地址未对齐。锁竞争不会返回`-1`。 |

## 约束说明

- 仅支持Ascend 950平台的AIV和`COMM_PROTOCOL_UBC_CTP`路径，不支持`COMM_PROTOCOL_ROCE`路径。
- 锁不可重入。释放锁前，不得对同一通道再次调用`Lock`。
- 所有更新通道状态的访问都需要由`Lock`和[Unlock](./Unlock.md)保护。
- 每次成功调用`Lock`后，都必须由同一AI Core调用`Unlock`，错误退出路径也需要释放锁。
- 仅用于同步同一设备上共享同一通道的AI Core。

## 调用示例

Lock接口的调用示例如下。

```cpp
if (hcomm.Lock(channel) == 0) {
    // 更新通道状态的操作
    hcomm.Unlock(channel);    // 释放锁
}
```

## 相关接口

- [Unlock](./Unlock.md)
- [Hcomm](./Hcomm.md)
