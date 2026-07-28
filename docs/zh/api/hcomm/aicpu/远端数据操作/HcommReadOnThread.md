# HcommReadOnThread

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
<!-- npu="310p" id4 -->
- Atlas 推理系列产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->

## 功能说明

头文件：[`#include "hcomm_primitives.h"`](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_primitives.h)。

从channel上的指定内存读数据，从源地址`src`读取长度为`len`的数据，并写入目的地址`dst`。接口调用方为目的地址`dst`所在节点，为异步接口。

## 函数原型

```cpp
int32_t HcommReadOnThread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，取值为通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄。<br>ThreadHandle类型定义可参考[ThreadHandle](../公共类型/ThreadHandle.md)。 |
| channel | 输入 | 通信通道句柄，取值为通过[HcclChannelAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)接口获取到的有效通信通道句柄。<br>ChannelHandle类型定义可参考[ChannelHandle](../公共类型/ChannelHandle.md)。 |
| dst | 输出 | 目标内存地址，使用[HcclGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclGetHcclBuffer.md)或[HcclChannelGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetHcclBuffer.md)获取到的内存。 |
| src | 输入 | 源内存地址，使用[HcclGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclGetHcclBuffer.md)或[HcclChannelGetHcclBuffer](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelGetHcclBuffer.md)获取到的内存。 |
| len | 输入 | 待读取的数据长度。单位：字节。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- `[dst, dst + len)`必须落在本端已注册或获取的内存范围内，`[src, src + len)`必须落在对端已导入或获取的内存范围内。

<!-- npu="950" id6 -->
- 针对Ascend 950PR/Ascend 950DT，仅支持AICPU_TS场景在Device侧调用，支持通信协议UB_CTP、UB_RTP、UBOE、RoCE。
<!-- end id6 -->

<!-- npu="A3,910b" id7 -->
- 针对如下产品型号：

    <!-- npu="A3" id8 -->
    Atlas A3 训练系列产品/Atlas A3 推理系列产品；
    <!-- end id8 -->

    <!-- npu="910b" id9 -->
    Atlas A2 训练系列产品/Atlas A2 推理系列产品；
    <!-- end id9 -->

    支持CPU_TS场景在Host侧调用，也支持AICPU_TS场景在Device侧调用。CPU_TS和AICPU_TS场景均支持通信协议HCCS、HCCS_ONLY、RoCE。
<!-- end id7 -->

## 调用示例

```cpp
// 申请通信线程资源。
CommEngine engine = CommEngine::COMM_ENGINE_CPU_TS; // Atlas A3 训练系列产品/Atlas A3 推理系列产品使用。
// Ascend 950PR/Ascend 950DT场景下，将engine设置为CommEngine::COMM_ENGINE_AICPU_TS。
uint32_t threadNum = 1;
uint32_t notifyNumPerThread = 1;
ThreadHandle thread;
HcclComm comm;
HcclThreadAcquire(comm, engine, threadNum, notifyNumPerThread, &thread);

// 申请通信通道资源。
uint32_t channelNum = 1;
HcclChannelDesc channelDesc;
HcclChannelDescInit(&channelDesc, channelNum);
ChannelHandle channel;
HcclChannelAcquire(comm, engine, &channelDesc, channelNum, &channel);

// 获取本端通信内存信息。
void * localBuffer;
uint64_t localBufferSize;
HcclGetHcclBuffer(comm, &localBuffer, &localBufferSize);

// 获取对端通信内存信息。
void * remoteBuffer;
uint64_t remoteBufferSize;
HcclChannelGetHcclBuffer(comm, channel, &remoteBuffer, &remoteBufferSize);
uint64_t len = std::min(localBufferSize, remoteBufferSize);

// 将对端内存的内容读到本端内存上。
HcommReadOnThread(thread, channel, localBuffer, remoteBuffer, len);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
