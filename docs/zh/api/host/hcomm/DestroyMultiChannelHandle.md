# DestroyMultiChannelHandle

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

释放通过`MakeMultiChannelHandle`的Hcomm Endpoint重载创建的多通道资源。

头文件为：

```cpp
#include "hcomm/hcomm_host.h"
```

## 函数原型

```cpp
HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel);
```

## 参数说明

**表1**  接口参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `multiChannel` | 输入 | Endpoint重载的`MakeMultiChannelHandle`返回的有效句柄。 |

## 返回值

成功返回`HCCL_SUCCESS`，失败返回对应的Hcomm错误码。

## 约束说明

- 调用前必须确保使用该句柄的Kernel已执行完成并同步。
- 本接口只能用于不带HCCL通信域的重载创建的句柄，并且应在`HcommEndpointDestroy`之前调用。`HcclComm`重载创建的资源由通信域自动释放，不能传入本接口。
- 每个多通道句柄只能销毁一次，销毁后不能继续使用。

## 调用示例

```cpp
MultiChannelHandle multiChannel; // 由Endpoint重载创建，且相关Kernel已执行并同步完成

HcommResult ret = DestroyMultiChannelHandle(multiChannel);
```
