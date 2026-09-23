# asccomm_write_reduce_with_notify_on_thread

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
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->
<!-- npu="310p" id6 -->
- Atlas 推理系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件路径：`"hcomm/asccomm_primitives.h"`。

向channel上的指定内存写数据，将src中长度为count\*sizeof\(dataType\)的内存数据，与dst所指向的相同长度的内存数据进行reduceOp操作，并将结果输出到dst中，并向dst所在节点发送同步信号。接口调用方为src所在节点，该接口为异步接口。

## 函数原型

```cpp
int32_t asccomm_write_reduce_with_notify_on_thread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp, uint32_t remoteNotifyIdx)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为通过`HcclThreadAcquire`接口（声明于`"hccl/hccl_res.h"`）获取到的threads。<br>ThreadHandle类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| channel | 输入 | 通信通道句柄，为通过`HcclChannelAcquire`接口（声明于`"hccl/hccl_channel.h"`）获取到的channels。<br>ChannelHandle类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| dst | 输出 | 目的内存地址，使用指定channel对端的HCCL通信内存。 |
| src | 输入 | 源内存地址，使用通信域中本端rank的HCCL通信内存。 |
| count | 输入 | 元素个数。 |
| dataType | 输入 | 数据类型。<br>HcommDataType类型的定义请参见`"hcomm/hcomm_primitives.h"`。<br>不同产品型号支持的数据类型定义不同，详细请参见[dataType说明](#dataType说明)。|
| reduceOp | 输入 | 归约操作类型，支持：sum、prod、max、min。<br>HcommReduceOp类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| remoteNotifyIdx | 输入 | 通信通道另一端的Notify索引。<br>取值范围：［0，notifyNum）。<br>notifyNum为`HcommChannelCreate`（声明于`"hcomm_channel.h"`）或`HcclChannelAcquire`（声明于`"hccl/hccl_channel.h"`）接口传入的channelDescs参数中的notifyNum。 |

### dataType说明

<!-- npu="950" id7 -->
针对Ascend 950PR/Ascend 950DT系列产品，支持的数据类型：int8、int16、int32、uint8、uint16、uint32、float16、float32、bfp16。
<!-- end id7 -->

## 返回值说明

int32_t：接口成功返回0，其他失败。

## 约束说明

该接口需要配合[asccomm_channel_notify_wait_on_thread](asccomm_channel_notify_wait_on_thread.md)使用。

在Ascend 950PR/Ascend 950DT系列产品上，仅支持AICPU_TS模式下、在Device侧调用该接口。

## 调用示例

```cpp
// 申请通信线程资源
CommEngine engine = CommEngine::COMM_ENGINE_AICPU_TS;
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

// 获取本端通信内存信息
void * localBuffer;
uint64_t localBufferSize;
HcclGetHcclBuffer(comm, &localBuffer, &localBufferSize);

// 获取对端通信内存信息
void * remoteBuffer;
uint64_t remoteBufferSize;
HcclChannelGetHcclBuffer(comm, channel, &remoteBuffer, &remoteBufferSize);

// 拷贝参数并Launch Kernel
// Device侧算法编排
uint64_t len = std::min(localBufferSize, remoteBufferSize);
uint64_t sizeOfFP32 = 4;
uint64_t count = len / sizeOfFP32;

// 将本端内存的内容写到对端内存上并通知对端
uint32_t rmtNotifyIdx = 0;
asccomm_write_reduce_with_notify_on_thread(thread, channel, remoteBuffer, localBuffer, count, HCOMM_DATA_TYPE_FP32, HCOMM_REDUCE_SUM, rmtNotifyIdx);

// 数据面操作
// ...

// 等待对端通知本端
uint32_t lclNotifyIdx = 0;
uint32_t notifyTimeout = 0;
asccomm_channel_notify_wait_on_thread(thread, channel, lclNotifyIdx, notifyTimeout);
```
