# HcommChannelFence

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

在指定通信通道上插入内存屏障操作，确保屏障前的通道读写操作在屏障后的通道读写操作之前完成。

## 函数原型

```cpp
int32_t HcommChannelFence(ChannelHandle channel)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| channel | 输入 | 通信通道句柄，取值为通过[HcommChannelCreate](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/basic_resource_mgmt/HcommChannelCreate.md)或[HcclChannelAcquire](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/control_plane_api/comms_domain_resource_mgmt/HcclChannelAcquire.md)接口获取到的有效通信通道句柄。<br>ChannelHandle类型定义可参考[ChannelHandle](../公共类型/ChannelHandle.md)。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

<!-- npu="950" id6 -->
- 本接口仅支持Ascend 950PR/Ascend 950DT。针对上述产品，本接口仅支持在Host CPU侧调用，不支持在AICPU侧调用，支持通信协议UB_RTP、UB_CTP、RoCE。
<!-- end id6 -->

## 调用示例

### 集合通信示例

```cpp
// 省略：创建通信域句柄comm。

// 申请通信通道资源。
CommEngine engine = CommEngine::COMM_ENGINE_CPU;
uint32_t channelNum = 1;
HcclChannelDesc channelDesc;
HcclChannelDescInit(&channelDesc, channelNum);
channelDesc.channelProtocol = COMM_PROTOCOL_ROCE;
channelDesc.localEndpoint.protocol = COMM_PROTOCOL_ROCE;
channelDesc.remoteEndpoint.protocol = COMM_PROTOCOL_ROCE;
// 省略：channelDesc填充其他信息。
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
HcommWriteNbi(channel, remoteBuffer, localBuffer, len);
HcommChannelFence(channel);
HcommReadNbi(channel, localBuffer, remoteBuffer, len);
```

### 基础通信示例

以Host RoCE H2D场景为例。
Client端为写发起端，提供Host源内存；Server端为写入目标端，提供Device目的内存。

#### Server端

```cpp
// 申请Server端Endpoint资源。
const EndpointDesc serverEndpointDesc = {
    .protocol = COMM_PROTOCOL_ROCE,
    .commAddr = {
        .type = COMM_ADDR_TYPE_IP_V4,
        .addr = {{192, 168, 1, 101}}
    },
    .loc = {
        .locType = ENDPOINT_LOC_TYPE_HOST
    },
    .raws = {0}
};
EndpointHandle serverEndpointHandle = nullptr;
HcommResult result = HcommEndpointCreate(&serverEndpointDesc, &serverEndpointHandle);

// 注册Server端内存。
CommMem serverDeviceMem = {
    .type = COMM_MEM_TYPE_DEVICE,
    .addr = serverDeviceAddr,
    .size = dataLen
};
HcommMemHandle serverMemHandle = nullptr;
result = HcommMemReg(serverEndpointHandle, "server_device_mem", &serverDeviceMem, &serverMemHandle);

// 导出Server端内存描述。
void *serverMemDesc = nullptr;
uint32_t serverMemDescLen = 0;
result = HcommMemExport(serverEndpointHandle, serverMemHandle, &serverMemDesc, &serverMemDescLen);

// 省略：
// Server端将serverEndpointDesc、serverMemDesc、serverMemDescLen发送给Client端。
// Server端获取clientEndpointDesc。

// 申请Server端通信通道资源。
HcommChannelDesc serverChannelDesc = {};
result = HcommChannelDescInit(&serverChannelDesc, 1);
serverChannelDesc.remoteEndpoint = clientEndpointDesc;
serverChannelDesc.notifyNum = 1;
serverChannelDesc.exchangeAllMems = true;
serverChannelDesc.role = HCOMM_SOCKET_ROLE_SERVER;
serverChannelDesc.roceAttr.queueNum = 1;
ChannelHandle serverChannel = 0;
result = HcommChannelCreate(serverEndpointHandle, COMM_ENGINE_CPU, &serverChannelDesc, 1, &serverChannel);
```

#### Client端

```cpp
// 申请Client端Endpoint资源。
const EndpointDesc clientEndpointDesc = {
    .protocol = COMM_PROTOCOL_ROCE,
    .commAddr = {
        .type = COMM_ADDR_TYPE_IP_V4,
        .addr = {{192, 168, 1, 100}}
    },
    .loc = {
        .locType = ENDPOINT_LOC_TYPE_HOST
    },
    .raws = {0}
};
EndpointHandle clientEndpointHandle = nullptr;
HcommResult result = HcommEndpointCreate(&clientEndpointDesc, &clientEndpointHandle);

// 注册Client端内存。
CommMem clientHostMem = {
    .type = COMM_MEM_TYPE_HOST,
    .addr = clientHostAddr,
    .size = dataLen
};
HcommMemHandle clientMemHandle = nullptr;
result = HcommMemReg(clientEndpointHandle, "client_host_mem", &clientHostMem, &clientMemHandle);

// 省略：
// Client端将clientEndpointDesc发送给Server端。
// Client端获取serverEndpointDesc、serverMemDesc、serverMemDescLen。

// 导入Server端内存描述。
CommMem importedServerDeviceMem = {};
result = HcommMemImport(clientEndpointHandle, serverMemDesc, serverMemDescLen, &importedServerDeviceMem);

// 申请Client端通信通道资源。
HcommChannelDesc clientChannelDesc = {};
result = HcommChannelDescInit(&clientChannelDesc, 1);
clientChannelDesc.remoteEndpoint = serverEndpointDesc;
clientChannelDesc.notifyNum = 1;
clientChannelDesc.exchangeAllMems = true;
clientChannelDesc.role = HCOMM_SOCKET_ROLE_CLIENT;
clientChannelDesc.roceAttr.queueNum = 1;
ChannelHandle clientChannel = 0;
result = HcommChannelCreate(clientEndpointHandle, COMM_ENGINE_CPU, &clientChannelDesc, 1, &clientChannel);

// 将Client端内存写入Server端内存。
int32_t ret = HcommWriteNbi(clientChannel, importedServerDeviceMem.addr, clientHostMem.addr, dataLen);

// 等待通信通道上已提交的写操作完成。
ret = HcommChannelFence(clientChannel);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
