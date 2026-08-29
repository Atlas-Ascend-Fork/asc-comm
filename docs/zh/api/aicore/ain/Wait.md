# Wait

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
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

## 功能说明

等待指定通信通道上的通信任务完成。

通常先调用`FlushAsync`获取指定peer的通信通道句柄，再调用`Wait`等待该通道上的已提交任务完成。

## 函数原型

```cpp
__aicore__ inline void Wait(AscendC::ChannelHandle& channelHandle);
```

## 参数说明

**表1**  接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channelHandle` | 输入 | 通信通道句柄，通常由`FlushAsync`返回。 |

## 返回值说明

无返回值。

## 约束说明

- `channelHandle`需要是有效通信通道句柄。
- `Wait`通过底层Hcomm完成等待接口阻塞等待该通道上已提交的任务完成。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。

## 调用示例

Wait接口的调用示例如下。

```cpp
AscendC::ChannelHandle channelHandle;
ain.Put(team, peer, dstWin, dstOffset, srcWin, srcOffset, bytes);
ain.FlushAsync(team, peer, &channelHandle);
ain.Wait(channelHandle);    // 等待指定通道任务完成
```
