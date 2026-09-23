# asccomm_channel_notify_wait_on_thread

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->
<!-- npu="310p" id6 -->
- Atlas 推理系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件路径：`"hcomm/asccomm_primitives.h"`。

等待同步信号，阻塞等待指定Channel上的Notify完成。

## 函数原型

```cpp
int32_t asccomm_channel_notify_wait_on_thread(ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。<br>ThreadHandle类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| channel | 输入 | 通信通道句柄，为通过`HcclChannelAcquire`接口（声明于`"hccl/hccl_channel.h"`）获取到的channels。关于channel的约束参见约束说明。<br>ChannelHandle类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| localNotifyIdx | 输入 | 本地Notify索引。<br>取值范围：［0，notifyNum）。<br>notifyNum为`HcommChannelCreate`（声明于`"hcomm_channel.h"`）或`HcclChannelAcquire`（声明于`"hccl/hccl_channel.h"`）接口传入的channelDescs参数中的notifyNum。 |
| timeOut | 输入 | 超时时间，单位：秒。<br> - 0：表示永久等待。<br> - >0：配置的具体超时时间。|

<!-- npu="950" id7 -->
**补充说明**：

- thread参数：

  针对Ascend 950PR/Ascend 950DT系列产品，CPU引擎RoCE场景下，“thread”参数无作用，传入0即可。CPU_TS/AICPU_TS场景下，“thread”参数为通过`HcclThreadAcquire`接口（声明于`"hccl/hccl_res.h"`）获取到的threads。

- timeOut参数：
  
  针对Ascend 950PR/Ascend 950DT系列产品的CPU引擎RoCE场景，需配置大于0的超时时间。
<!-- end id7 -->

## 返回值说明

int32_t：接口成功返回0，其他失败。

## 约束说明

- 该接口需要配合[asccomm_channel_notify_record_on_thread](asccomm_channel_notify_record_on_thread.md)使用。
<!-- npu="950" id8 -->
- 针对Ascend 950PR/Ascend 950DT系列产品，支持AICPU_TS场景在Device侧调用，也支持CPU引擎RoCE场景在Host CPU侧调用。
- 针对Ascend 950PR/Ascend 950DT系列产品的CPU引擎RoCE场景，调用`HcclChannelAcquire`申请入参channel时，需传入`engine = COMM_ENGINE_CPU`，且`channelDesc.remoteEndpoint.protocol = COMM_PROTOCOL_ROCE`。URMA/UBC等协议通道当前不支持该接口。
- 针对Ascend 950PR/Ascend 950DT系列产品的CPU引擎RoCE场景，`localNotifyIdx`必须小于本端通信通道的Notify数量，且通信通道创建时的`notifyNum`需大于0；`timeOut`需大于0。
<!-- end id8 -->
- Host CPU侧调用时，`thread`参数无作用，可传入0。

## 调用示例

```cpp
// 申请通信线程资源
CommEngine engine = CommEngine::COMM_ENGINE_CPU_TS;
// Ascend 950PR/Ascend 950DT系列产品时配置
// CommEngine engine = CommEngine::COMM_ENGINE_AICPU_TS;
uint32_t threadNum = 1;
uint32_t notifyNumPerThread = 1;
ThreadHandle thread;
HcclComm comm;
HcclThreadAcquire(comm, engine, threadNum, notifyNumPerThread, &thread);

// 申请通信通道资源
uint32_t channelNum = 1;
HcclChannelDesc channelDesc;
HcclChannelDescInit(&channelDesc, channelNum);
ChannelHandle channel;
HcclChannelAcquire(comm, engine, &channelDesc, channelNum, &channel);

// 针对Ascend 950PR/Ascend 950DT系列产品，需要在Device侧调用以下接口

// 通知对端
asccomm_channel_notify_record_on_thread(thread, channel, 0);

// 数据面操作
// ...

// 等待对端通知本端
uint32_t notifyTimeout = 1800;
asccomm_channel_notify_wait_on_thread(thread, channel, 0, notifyTimeout);
```
