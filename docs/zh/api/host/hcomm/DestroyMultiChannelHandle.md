# DestroyMultiChannelHandle

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

头文件路径：`"hcomm/hcomm_host.h"`。

释放通过[MakeMultiChannelHandle](./MakeMultiChannelHandle.md)的Endpoint重载创建的多通道资源。

## 函数原型

```cpp
HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel);
```

## 参数说明

**表1** 接口参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `multiChannel` | 输入 | Endpoint重载的`MakeMultiChannelHandle`返回的有效句柄。 |

## 返回值说明

成功时返回`HCCL_SUCCESS`，失败时返回对应的Hcomm错误码。主要错误码及成因如下：

**表2** 返回错误码说明

| 错误码 | 成因 |
| --- | --- |
| `HCCL_E_PTR` | `multiChannel`为0（无效句柄）。 |
| `HCCL_E_PARA` | 句柄实体中通道数量无效。 |
| `HCCL_E_RUNTIME` | 从Device拷贝句柄实体或释放资源时ACL运行时调用失败。 |
| `HCCL_E_INTERNAL` | 多通道句柄实体字段校验失败。 |
| `HCCL_E_MEMORY` | Host侧临时内存分配失败。 |

## 约束说明

- 调用前必须确保使用该句柄的Kernel已执行完成并同步。
- 本接口只能用于Endpoint重载创建的句柄，并且应在`HcommEndpointDestroy`之前调用。HCCL（Huawei Collective Communication Library，华为集合通信库）通信域重载创建的资源由通信域自动释放，不能传入本接口。
- 每个多通道句柄只能销毁一次，销毁后不能继续使用。

## 调用示例

DestroyMultiChannelHandle接口的调用示例如下。

```cpp
// 前置条件：endpointHandle为已初始化的Hcomm Endpoint，multiChannel为其MakeMultiChannelHandle重载创建的多通道句柄；
// HcommEndpointDestroy为Hcomm Endpoint的配套销毁接口，返回HcommResult
HcommResult DestroyChannels(EndpointHandle endpointHandle, MultiChannelHandle multiChannel)
{
    // 使用multiChannel的Kernel执行完成并同步后，销毁多通道资源
    HcommResult ret = DestroyMultiChannelHandle(multiChannel);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    // 多通道资源销毁完成后，才能销毁创建该资源的Hcomm Endpoint
    return HcommEndpointDestroy(endpointHandle);
}
```

