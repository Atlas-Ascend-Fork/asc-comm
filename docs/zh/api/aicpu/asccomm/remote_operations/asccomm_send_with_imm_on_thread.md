# asccomm_send_with_imm_on_thread

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

头文件路径：`"aicpu/asccomm/asccomm_primitives.h"`。

在指定Channel上发起携带立即数的双边消息发送：将本端`src`指向的`len`字节数据发送到对端，同时随消息携带64位立即数`immData`。与`asccomm_send_on_thread`相比，本接口额外传递一个立即数。接口调用方为`src`所在节点。

本接口是send-recv语义中的send侧接口，必须与接收端匹配的recv接口共同使用；本接口不创建接收任务，也不指定接收端内存地址。当前Asccomm公开接口暂不提供recv接口，接收侧任务需由上层通信框架或后续提供的接收接口拉起。

立即数语义：`immData`不占用`len`所描述的消息数据载荷，也不写入远端通信内存，而是随硬件完成事件传递给接收端，可用于携带消息长度、序号等控制信息。

### send-recv使用流程

通用send-recv使用流程见[asccomm_send_on_thread](asccomm_send_on_thread.md#send-recv使用流程)。

## 函数原型

```cpp
int32_t asccomm_send_with_imm_on_thread(ThreadHandle thread, ChannelHandle channel, const void *src, uint64_t len, uint64_t immData)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，包含`"aicpu/asccomm/asccomm_primitives.h"`后即可使用。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，包含`"aicpu/asccomm/asccomm_primitives.h"`后即可使用。 |
| src | 输入 | 本端通信内存地址，提供待发送的数据。 |
| len | 输入 | 数据长度（字节）。 |
| immData | 输入 | 64位立即数，取值为任意64位无符号整数。随消息的完成事件传递给接收端，不占用消息数据载荷。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 发送请求处理成功。[UB类协议](../../../../guide/programming_model/communication_pattern_selection.md#通信协议分类与选择)表示发送任务已提交。 |
| `ASCCOMM_E_PTR` | `src`为空、Thread或Channel句柄为0，或所需资源地址为空。 |
| `ASCCOMM_E_PARA` | Thread或Channel资源、UB通信上下文或内存保护参数不合法。 |
| `ASCCOMM_E_NOT_SUPPORT` | Channel协议不是当前支持的UB类协议。 |
| `ASCCOMM_E_TIMEOUT` | 通信任务提交队列等待超时。 |
| `ASCCOMM_E_INTERNAL` | 任务、队列等内部异常。 |

## 约束说明

- 本接口仅支持UB类协议，RoCE等其他协议返回`ASCCOMM_E_NOT_SUPPORT`。
- `src`应使用该Channel对应的本端通信内存地址，`[src, src + len)`应落在本端已注册的通信内存范围内。
- `len`无固定单次上限，超过单条消息硬件传输上限的数据由接口内部自动分段发送，调用方无需自行拆分。
- `len`为0时，本接口仍会向对端下发一条零长度消息（`src`仍需为有效的本端通信内存地址），`immData`随该消息的完成事件正常传递给接收端，可用于纯控制消息场景。
- `immData`不写入远端通信内存，接收端通过完成事件获取立即数。
- 返回`ASCCOMM_SUCCESS`仅表示发送任务已提交，不表示对端已完成接收。

## 调用示例

```cpp
// thread为控制面传入的有效ThreadHandle资源句柄；channel为控制面传入的有效ChannelHandle资源句柄。
// src为本端通信内存地址；immData为64位立即数。
uint64_t len = 1024;
uint64_t immData = 0x1234;
int32_t ret = asccomm_send_with_imm_on_thread(thread, channel, src, len, immData);
```
