# HcommFenceOnThread

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
<!-- npu="310p" id4 -->
- Atlas 推理系列产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->

## 功能说明

头文件：[`#include "hcomm_primitives.h"`](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_primitives.h)。

在Host CPU侧对当前已初始化的所有数据面Flush资源执行全局Flush，并等待Flush完成。该接口不绑定具体通信线程或通信通道；如需等待指定通信通道上已提交的读写操作完成，应先调用[HcommChannelFenceOnThread](HcommChannelFenceOnThread.md)。

## 函数原型

```cpp
int32_t HcommFenceOnThread(ThreadHandle thread)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。取值为0或通过[HcclThreadAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclThreadAcquire.md)接口获取到的有效线程句柄；Host CPU侧调用时，该参数无作用，传入0即可。<br>[ThreadHandle](../公共类型/ThreadHandle.md)类型定义可参考对应公共类型文档。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

<!-- npu="950" id6 -->
- 接口仅支持Ascend 950PR/Ascend 950DT，仅支持Host CPU侧调用。
- Host CPU侧调用时，仅支持通信协议RoCE，`thread`参数无作用，可传入0。
- 该接口用于Host CPU侧数据面Flush，不等待指定通信通道上的读写操作完成。如需等待通道上已提交的读写操作完成，应先调用[HcommChannelFenceOnThread](HcommChannelFenceOnThread.md)。
- 当前没有需要执行Flush的内部资源时，接口返回成功。
<!-- end id6 -->
## 调用示例

### 集合通信示例

```cpp
// 申请通信通道资源。
CommEngine engine = CommEngine::COMM_ENGINE_CPU;
uint32_t channelNum = 1;
HcclChannelDesc channelDesc;
HcclChannelDescInit(&channelDesc, channelNum);
HcclComm comm;
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

// 将本端内存的内容写到对端内存上。
int32_t ret = HcommWriteNbiOnThread(0, channel, remoteBuffer, localBuffer, len);
ret = HcommChannelFenceOnThread(0, channel);

// 执行Host CPU侧数据面Flush并等待完成。
ret = HcommFenceOnThread(0);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
