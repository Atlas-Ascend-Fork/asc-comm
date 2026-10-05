# asccomm_batch_transfer_on_thread

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

头文件路径：`"aicpu/hcomm/asccomm_primitives.h"`。

在指定线程和通道上异步提交一组传输任务。每个传输任务由`AsccommBatchTransferDesc`描述，定义位于`"aicpu/hcomm/asccomm_res_defs.h"`，支持单边写、单边读、写归约、归约读、带通知的写、带通知的归约写以及通知记录等操作类型。

## 函数原型

```cpp
int32_t asccomm_batch_transfer_on_thread(ThreadHandle thread, ChannelHandle channel,
    const AsccommBatchTransferDesc* transferDescs, uint32_t transferDescNum)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，定义见`"aicpu/hcomm/hcomm_res_defs.h"`。 |
| transferDescs | 输入 | 批量传输描述符数组，数组中每个元素描述一个传输任务，按数组顺序依次提交。类型为`AsccommBatchTransferDesc`，定义见`"aicpu/hcomm/asccomm_res_defs.h"`。 |
| transferDescNum | 输入 | 描述符数量，须大于0。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 传输任务处理并提交成功。 |
| `ASCCOMM_E_PARA` | 参数或通信资源配置不合法。 |
| `ASCCOMM_E_PTR` | 句柄、指针或任务地址为空或无效。 |
| `ASCCOMM_E_NOT_SUPPORT` | Channel使用了当前实现不支持的协议。 |
| `ASCCOMM_E_TIMEOUT` | 立即提交时通信队列空间不足且等待超时。 |
| `ASCCOMM_E_INTERNAL` | 发生内部执行异常。 |

## 约束说明

- `transferDescs`不能为NULL，`transferDescNum`必须大于0。
- 当前实现仅支持UB类协议通道；RoCE通道返回`ASCCOMM_E_NOT_SUPPORT`。
- `transType`仅支持写、读、写归约、读归约、带通知写、带通知写归约和通知记录。
- 除`ASCCOMM_TRANSFER_TYPE_NOTIFY_RECORD`外，每个描述符的源地址和目的地址均不能为NULL，且地址范围须落在通道已注册的通信内存范围内。
- 带通知的传输任务和通知记录任务中的`notifyIdx`必须对应Channel的有效远端Notify资源。

## 调用示例

```cpp
// 前置条件：thread和channel为控制面传入的有效Thread/Channel资源句柄。
// localBuffer/localBuffer2为本端已注册通信内存地址，remoteBuffer/remoteBuffer2为对端已注册通信内存地址。

// 构造批量传输描述符
AsccommBatchTransferDesc descs[2] = {};

// 描述符1：单边写
descs[0].transType = ASCCOMM_TRANSFER_TYPE_WRITE;
descs[0].transferInfo.write.len = 4096;
descs[0].transferInfo.write.dst = remoteBuffer;    // 远端地址
descs[0].transferInfo.write.src = localBuffer;     // 本地地址

// 描述符2：单边写（带通知）
descs[1].transType = ASCCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY;
descs[1].transferInfo.writeWithNotify.len = 8192;
descs[1].transferInfo.writeWithNotify.dst = remoteBuffer2;    // 远端地址
descs[1].transferInfo.writeWithNotify.src = localBuffer2;     // 本地地址
descs[1].transferInfo.writeWithNotify.notifyIdx = 0;          // 通知索引

// 提交批量传输。此处省略错误处理，实际调用时应检查ret
int32_t ret = asccomm_batch_transfer_on_thread(thread, channel, descs, 2);
(void)ret;
```
