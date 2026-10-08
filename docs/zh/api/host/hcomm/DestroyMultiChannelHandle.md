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

释放通过[MakeMultiChannelHandle](./MakeMultiChannelHandle.md)的Hcomm Endpoint重载创建的多通道资源。

## 函数原型

```cpp
HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `multiChannel` | 输入 | Endpoint重载的`MakeMultiChannelHandle`返回的有效句柄。 |

## 返回值说明

成功返回`HCCL_SUCCESS`，失败返回对应的Hcomm错误码。

## 约束说明

- 调用前必须确保使用该句柄的Kernel已执行完成并同步。
- 本接口只能用于不带HCCL通信域的重载创建的句柄，并且应在`HcommEndpointDestroy`之前调用。`HcclComm`重载创建的资源由通信域自动释放，不能传入本接口。
- 每个多通道句柄只能销毁一次，销毁后不能继续使用。

## 调用示例

DestroyMultiChannelHandle接口的调用示例如下。

```cpp
// 使用该句柄的Kernel执行并同步完成后，销毁多通道资源
DestroyMultiChannelHandle(multiChannel);
```
