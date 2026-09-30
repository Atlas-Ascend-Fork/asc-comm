# asccomm_batch_transfer_on_thread

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

头文件路径：`"asccomm/asccomm_primitives.h"`。

在指定线程和通道上异步提交一组传输任务。每个传输任务由`HcommBatchTransferDesc`描述，定义位于`"asccomm/hcomm_res_defs.h"`，支持单边写、单边读、写归约、归约读、带通知的写、带通知的归约写以及通知记录等操作类型。

## 函数原型

```cpp
int32_t asccomm_batch_transfer_on_thread(ThreadHandle thread, ChannelHandle channel,
    const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为控制面构造并随AICPU Kernel参数下发的有效Thread资源句柄，当前实现要求该参数不能为0。<br>ThreadHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄，为控制面构造并随AICPU Kernel参数下发的有效Channel资源句柄，当前实现要求该参数不能为0。<br>ChannelHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| transferDescs | 输入 | 批量传输描述符数组，数组中每个元素描述一个传输任务，按数组顺序依次提交。描述符定义参见`"asccomm/hcomm_res_defs.h"`。 |
| transferDescNum | 输入 | 描述符数量，须大于0。 |

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- 当前实现仅支持UB类协议通道；RoCE通道返回`ASCCOMM_E_NOT_SUPPORT`。
- `transferDescNum`为0或`transferDescs`为NULL时返回`ASCCOMM_E_PARA`或`ASCCOMM_E_PTR`。
- 除`HCOMM_TRANSFER_TYPE_NOTIFY_RECORD`外，每个描述符的源地址和目的地址均不能为NULL，且地址范围须落在通道已注册的RMA内存范围内。
- 通知类描述符的`notifyIdx`必须小于通道远端Notify数量。
- 不支持的`transType`返回`ASCCOMM_E_PARA`。

## 调用示例

```cpp
// 申请通信线程资源和通道资源（略）
// 获取本端和对端通信内存信息（略）

// 构造批量传输描述符
HcommBatchTransferDesc descs[2];

// 描述符1：单边写
descs[0].transType = HCOMM_TRANSFER_TYPE_WRITE;
descs[0].transferInfo.write.len = 4096;
descs[0].transferInfo.write.dst = remoteBuffer;    // 远端地址
descs[0].transferInfo.write.src = localBuffer;     // 本地地址

// 描述符2：单边写（带通知）
descs[1].transType = HCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY;
descs[1].transferInfo.writeWithNotify.len = 8192;
descs[1].transferInfo.writeWithNotify.dst = remoteBuffer2;    // 远端地址
descs[1].transferInfo.writeWithNotify.src = localBuffer2;     // 本地地址
descs[1].transferInfo.writeWithNotify.notifyIdx = 0;          // 通知索引

// 提交批量传输
asccomm_batch_transfer_on_thread(thread, channel, descs, 2);
```
