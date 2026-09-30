# asccomm_write_with_notify_on_thread

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

头文件路径：`"asccomm/asccomm_primitives.h"`。

向channel上的指定内存写数据，将src中长度为len的内存数据写入dst所指向的相同长度的内存区域，并且向dst所在节点发送同步信号。接口调用方为src所在节点，该接口为异步接口。

## 函数原型

```cpp
int32_t asccomm_write_with_notify_on_thread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len, uint32_t remoteNotifyIdx)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为控制面构造并随AICPU Kernel参数下发的有效Thread资源句柄，当前实现要求该参数不能为0。<br>ThreadHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄，为控制面构造并随AICPU Kernel参数下发的有效Channel资源句柄，当前实现要求该参数不能为0。<br>ChannelHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| dst | 输出 | 目的内存地址，通道已注册RMA内存中的地址。 |
| src | 输入 | 源内存地址，通道已注册RMA内存中的地址。 |
| len | 输入 | 数据长度（字节）。 |
| remoteNotifyIdx | 输入 | 通信通道另一端的Notify索引。<br>取值范围：［0，notifyNum）。<br>notifyNum为Channel资源中的对应Notify数量。 |

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- 本接口同时支持UB类协议和RoCE协议通道。
- src和dst对应的地址范围须分别落在通道本端与远端已注册RMA内存范围内。
- `remoteNotifyIdx`必须小于通道远端Notify数量。

## 调用示例

```cpp
uint32_t remoteNotifyIdx = 0;
int32_t ret = asccomm_write_with_notify_on_thread(
    thread, channel, dst, src, len, remoteNotifyIdx);
```
