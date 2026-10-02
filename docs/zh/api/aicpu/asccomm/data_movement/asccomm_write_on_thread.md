# asccomm_write_on_thread

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

在指定Channel上发起单边写，将本端`src`指向的`len`字节数据写入远端`dst`指向的内存。接口调用方为`src`所在节点。

## 函数原型

```cpp
int32_t asccomm_write_on_thread(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，定义见`"asccomm/hcomm_res_defs.h"`。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，定义见`"asccomm/hcomm_res_defs.h"`。 |
| dst | 输出 | 远端目标地址，用于接收写入的数据。 |
| src | 输入 | 本端源地址，提供待写入的数据。 |
| len | 输入 | 数据长度（字节）。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 写请求处理成功。UB通道表示写任务已提交；RoCE通道在返回前检查写操作完成结果。 |
| `ASCCOMM_E_PTR` | `dst`或`src`为空、Thread或Channel句柄为0，或所需资源地址为空。 |
| `ASCCOMM_E_PARA` | Thread或Channel资源ABI、通信上下文或内存保护参数不合法。 |
| `ASCCOMM_E_NOT_SUPPORT` | Channel协议既不是当前支持的UB类协议，也不是RoCE协议。 |
| `ASCCOMM_E_TIMEOUT` | RTSQ或RoCE队列等待超时，或RoCE完成事件轮询超时。 |
| `ASCCOMM_E_REMOTE` | RoCE完成事件报告远端通信错误。 |
| `ASCCOMM_E_INTERNAL` | 内部异常，或任务、队列、Doorbell等内部操作失败。 |

## 约束说明

- 本接口同时支持UB类协议和RoCE协议通道。
- `src`和`dst`应使用该Channel对应的本端和远端通信内存地址。
- `dst`地址范围必须落在远端已注册的通信内存范围内。

## 调用示例

```cpp
// thread、channel、src和dst已由前置流程准备；src为本端通信内存地址，dst为远端通信内存地址。
uint64_t len = 1024;
int32_t ret = asccomm_write_on_thread(thread, channel, dst, src, len);
```
