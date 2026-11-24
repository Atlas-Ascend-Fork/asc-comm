# make_multi_channel_handle

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

头文件路径：`"asccomm/asccomm_host.h"`。

创建批量接口多通道模式所需的通信资源，并返回Device侧使用的`multi_channel_handle`。接口提供HCCL（Huawei Collective Communication Library，华为集合通信库）通信域和Hcomm Endpoint（Hcomm通信端点，不依赖HCCL通信域的独立通信端点组件，由外部asccomm组件提供）两种重载。

## 函数原型

HCCL通信域重载：

```cpp
HcclResult make_multi_channel_handle(
    HcclComm comm,
    const char* shared_queue_tag,
    const HcclChannelDesc* channel_descs,
    uint32_t channel_num,
    multi_channel_handle* multi_channel);
```

Endpoint重载：

```cpp
HcommResult make_multi_channel_handle(
    EndpointHandle endpoint_handle,
    HcommChannelDesc* channel_descs,
    uint32_t channel_num,
    multi_channel_handle* multi_channel);
```

## 参数说明

**表1** 接口参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `comm` | 输入 | 已初始化的HCCL通信域。 |
| `shared_queue_tag` | 输入 | 本组多通道资源的标签，在同一通信域内必须唯一。 |
| `endpoint_handle` | 输入 | 已初始化且支持AIV（AI Vector，向量核）引擎`COMM_PROTOCOL_UB_CTP`协议的Hcomm Endpoint。 |
| `channel_descs` | 输入 | 包含`channel_num`个元素的HCCL或Hcomm通道描述数组，参见[HcclChannelDesc](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/HcclChannelDesc.md)和[HcommChannelDesc](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/HcommChannelDesc.md)。数组下标对应Device侧[get_handle_ref](../../aicore/asccomm/get_handle_ref.md)的`channel_idx`。 |
| `channel_num` | 输入 | 通道描述数量，必须大于0。 |
| `multi_channel` | 输出 | Device侧多通道句柄，须指向有效可写地址（不能为`nullptr`）。创建失败时置为0。 |

## 返回值说明

HCCL通信域重载成功时返回`HCCL_SUCCESS`，失败时返回对应的HCCL错误码；Endpoint重载成功时返回`HCCL_SUCCESS`，失败时返回对应的Hcomm错误码。主要错误码及成因如下：

**表2** 返回错误码说明

| 错误码 | 成因 |
| --- | --- |
| `HCCL_E_PARA` | 参数无效：`multi_channel`、`comm`或`endpoint_handle`为空指针，`shared_queue_tag`或`channel_descs`为空，`channel_num`为0。 |
| `HCCL_E_MEMORY` | Host侧内存分配失败。 |
| `HCCL_E_TIMEOUT` | Endpoint重载等待通道就绪超时（重试约120秒后失败）。 |
| `HCCL_E_INTERNAL` | 通道连接失败或内部状态异常（两种重载均可能）。 |
| `HCCL_E_RUNTIME` | ACL运行时调用失败。 |
| 其他HCCL错误码 | 由HCCL通道获取、通道配置等内部调用传播（两种重载均可能）。 |

## 约束说明

- 所有输入指针均不能为`nullptr`，`channel_num`必须大于0。
- HCCL通信域重载要求`comm`已完成初始化，并在Kernel使用`multi_channel`句柄期间保持有效。同一通信域内，每个`shared_queue_tag`只能用于一次成功调用。
- 通信域重载中，通道描述的协议（`HcclChannelDesc.channelProtocol`）必须与Kernel侧`asc::asccomm`模板协议一致，详见[CommProtocol](../../aicore/asccomm/CommProtocol.md)。
- HCCL通信域重载创建的资源由`comm`管理，并随通信域销毁自动释放，不能调用[destroy_multi_channel_handle](./destroy_multi_channel_handle.md)释放。
- Endpoint重载要求`endpoint_handle`已完成初始化并支持`COMM_ENGINE_AIV`和`COMM_PROTOCOL_UB_CTP`协议。
- Endpoint重载创建的资源由调用方管理。使用该句柄的Kernel执行并同步完成后，必须调用`destroy_multi_channel_handle`，然后才能销毁`endpoint_handle`。
- 创建的多通道资源不能被并发使用。
- `channel_descs`在Host侧的数组下标必须与Device侧传给`asccomm.get_handle_ref`的`channel_idx`保持一致。

## 调用示例

make_multi_channel_handle接口的调用示例如下，HCCL通信域重载完整样例参见[02_asccomm_urma_batch_write](../../../../../examples/aicore/asccomm/02_asccomm_urma_batch_write/README.md)。

HCCL通信域重载：

```cpp
// comm为已初始化的HCCL通信域，channel_descs为已填充的通道描述数组
HcclResult make_channels_by_comm(
    HcclComm comm, const HcclChannelDesc* channel_descs, uint32_t channel_num,
    multi_channel_handle* multi_channel)
{
    // shared_queue_tag在本组多通道资源所属通信域内必须唯一
    const char* shared_queue_tag = "hcomm_shared_queue_0";

    // 创建多通道资源并返回Device侧使用的多通道句柄
    // 该重载创建的资源由comm管理，随通信域销毁自动释放，不能调用destroy_multi_channel_handle
    return make_multi_channel_handle(
        comm, shared_queue_tag, channel_descs, channel_num, multi_channel);
}
```

Endpoint重载：

```cpp
// endpointHandle为已初始化且支持AIV引擎COMM_PROTOCOL_UB_CTP协议的Endpoint，channel_descs为已填充的通道描述数组
HcommResult make_channels_by_endpoint(
    EndpointHandle endpoint_handle, HcommChannelDesc* channel_descs, uint32_t channel_num,
    multi_channel_handle* multi_channel)
{
    // 创建多通道资源并返回Device侧使用的多通道句柄
    // 该重载创建的资源由调用方管理，Kernel执行并同步完成后需调用destroy_multi_channel_handle释放
    return make_multi_channel_handle(
        endpoint_handle, channel_descs, channel_num, multi_channel);
}
```
