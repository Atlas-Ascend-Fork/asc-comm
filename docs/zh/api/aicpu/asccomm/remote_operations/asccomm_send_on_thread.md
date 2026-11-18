# asccomm_send_on_thread（实验）

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

头文件路径：`"aicpu/asccomm/experimental/asccomm_experimental_primitives.h"`。

本接口为实验性接口，不作为对外商用接口提供，不保证版本间兼容性，后续版本可能变更或移除。

本接口在指定Channel上发起双边消息发送，将本端`src`指向的`len`字节数据发送至对端，接收端从该Channel的接收上下文中获取数据。接口调用方为`src`所在节点。与单边写接口（如`asccomm_write_on_thread`）不同，本接口不指定远端目的地址，消息的实际接收位置由接收端已投递的recv（接收请求）决定。

本接口实现send/recv语义中的发送侧能力。send/recv属于双边消息传输机制：发送端发起send操作时仅指定本地待发送数据；接收端须提供本地接收缓冲区，并保证recv请求在send数据到达接收端前处于已投递状态。本接口仅提供发送侧send能力，既不创建也不管理接收侧recv任务，亦不指定或感知接收端内存地址。每条消息匹配并消耗一个已投递且尚未被消费的recv条目；通信硬件将该消息的数据写入该recv条目指向的接收缓冲区。recv条目完成匹配后即被消耗，不能用于后续消息。

本组件不提供接收侧recv接口，也不负责接收侧任务的投递、补充与生命周期管理。接收侧recv任务须由调用方实现，并保证在send数据到达前存在数量充足、缓冲区大小匹配的已投递recv。

### 使用流程

1. 控制面建链与资源准备：通信双方通过控制面建立连接，确保发送端与接收端具备可用的通信上下文。发送端须将以`src`为起始地址、长度为`len`的本端通信内存注册为RMA/URMA保护并绑定到Channel；本端注册Buffer信息缺失时，本接口返回`ASCCOMM_E_PTR`；保护类型不是URMA时，本接口返回`ASCCOMM_E_PARA`。接收端控制面须为Channel配置send/recv专用的对端接收Jetty目标描述，Jetty为UB协议使用的通信队列标识。当前实现通过地址为0且长度为0的URMA注册Buffer描述该目标，普通远端注册内存描述不能替代该描述；描述缺失或不可识别时，本接口返回`ASCCOMM_E_PARA`。
2. 接收端投递recv：接收端通过用户实现的接收侧任务投递recv请求，并提供足以容纳对应send操作数据的接收缓冲区。recv条目按投递顺序进入等待状态，等待与后续消息匹配。
3. 发送端提交send：发送端调用本接口提交send任务。send操作不指定接收端内存地址；消息到达接收端后匹配并消耗一个已投递且尚未被消费的recv条目，通信硬件将数据写入该recv条目指定的接收缓冲区。
4. 发送侧接口返回：本接口完成发送任务生成后返回`ASCCOMM_SUCCESS`。任务下发时机由Thread资源的提交策略决定：在自动提交模式下可立即提交；在批量提交模式下先进入发送侧本地缓存，待缓存满或调用`asccomm_batch_mode_end`后提交。返回值仅表示请求处理成功，不表示任务已下发至硬件，也不表示接收端已完成接收。
5. 发送侧提交缓存任务：若使用[asccomm_batch_mode_start](../other_operations/asccomm_batch_mode_start.md)开启批量提交模式，等待接收端完成事件前须先调用[asccomm_batch_mode_end](../other_operations/asccomm_batch_mode_end.md)提交当前线程尚未下发的缓存任务。批量缓存满时可能已提前提交，此时仍可正常调用End。
6. 接收端处理完成事件：接收端等待recv完成事件，确认recv完成后从接收缓冲区读取数据。

## 函数原型

```cpp
int32_t asccomm_send_on_thread(ThreadHandle thread, ChannelHandle channel, const void* src, uint64_t len)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，包含`"aicpu/asccomm/experimental/asccomm_experimental_primitives.h"`后即可使用。 |
| channel | 输入 | 通信通道句柄。控制面创建Channel资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ChannelHandle`，包含`"aicpu/asccomm/experimental/asccomm_experimental_primitives.h"`后即可使用。 |
| src | 输入 | 本端通信内存地址，指向待发送数据。 |
| len | 输入 | 数据长度，单位为字节。本接口统一校验单次发送长度上限65536字节（64KB），超过该值时返回`ASCCOMM_E_PARA`，且不生成发送任务。该上限为接口层统一校验值，不代表当前Channel协议的有效上限。当前实现接受[UB_CTP协议](../../../../guide/programming_model/communication_pattern_selection.md#通信协议分类与选择)（含旧名UBC_CTP）、UBC_TP、UB Memory、UBoE、[UB_RTP协议](../../../../guide/programming_model/communication_pattern_selection.md#通信协议分类与选择)（含旧名UBG）作为UB类协议；其中UB_CTP的有效上限为4KB，UB_RTP的有效上限为64KB，其他协议的有效单消息上限由底层协议规格定义，调用方仍须保证`len`不超过当前Channel协议的有效上限。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 发送请求处理成功。发送任务已生成，是否立即提交由Thread资源的提交策略决定。 |
| `ASCCOMM_E_PTR` | `src`为空、Thread或Channel句柄为0、本端注册Buffer信息缺失，或所需资源地址为空。 |
| `ASCCOMM_E_PARA` | Thread或Channel资源、UB通信上下文或内存保护参数不合法，本端发送内存保护不是URMA，Channel中缺失或存在无效的send/recv专用对端接收Jetty目标描述，或`len`超出单次发送长度上限（65536字节）。 |
| `ASCCOMM_E_NOT_SUPPORT` | Channel协议不是当前支持的UB类协议。 |
| `ASCCOMM_E_TIMEOUT` | 通信任务提交队列等待超时。 |
| `ASCCOMM_E_INTERNAL` | 任务、队列等内部异常。 |

## 约束说明

- 当前实现仅支持UB类协议，包括[UB_CTP协议](../../../../guide/programming_model/communication_pattern_selection.md#通信协议分类与选择)（含旧名UBC_CTP）、UBC_TP、UB Memory、UBoE、[UB_RTP协议](../../../../guide/programming_model/communication_pattern_selection.md#通信协议分类与选择)（含旧名UBG）；[RoCE协议](../../../../guide/programming_model/communication_pattern_selection.md#通信协议分类与选择)及其他协议返回`ASCCOMM_E_NOT_SUPPORT`。
- 发送前置条件：发送端须将以`src`为起始地址、长度为`len`的本端通信内存注册为RMA/URMA保护并绑定到Channel；接收端控制面须为Channel配置send/recv专用的对端接收Jetty目标描述。当前实现通过地址为0且长度为0的URMA注册Buffer描述该目标，数据面不解析或复用普通远端注册内存描述。
- send与recv按消息一一匹配：一条消息消耗一个recv，recv不可复用；消息到达接收端时，须存在用户实现的接收侧任务投递的可匹配recv，且该recv的接收缓冲区可容纳对应消息的数据长度。
- 接口层统一校验单次发送长度上限65536字节（64KB）。当前版本不支持拆包，每次调用仅生成一条WQE（Work Queue Element，工作队列元素），对应一条线上消息，匹配并消耗一个recv，并产生一个完成事件。
- 除上述64KB统一校验外，数据面不校验Channel协议对应的单消息有效上限。UB_CTP的有效上限为4KB，UB_RTP的有效上限为64KB；UBC_TP、UB Memory、UBoE的有效单消息上限由底层协议规格定义。调用方须自行保证`len`不超过当前Channel协议的有效上限；超过该有效上限时，发送行为未定义，调用方须将超长数据拆分为多次send调用，每次调用对应一条独立消息并各消耗一个recv。
- `src`应为该Channel对应的本端通信内存地址；以`src`为起始地址、长度为`len`的数据区间须完整位于本端已注册的通信内存范围内。
- `len`为0时，本接口仍向对端下发一条零长度消息；`src`仍须为有效的本端通信内存地址。该零长度消息同样消耗一个recv并产生一个完成事件。
- 发送任务提交时机与Thread资源的提交策略一致：自动提交模式下本接口会触发任务提交；批量提交模式下任务先进入发送侧本地缓存，待缓存满或调用[asccomm_batch_mode_end](../other_operations/asccomm_batch_mode_end.md)后提交。等待对端recv完成前须先调用该接口提交剩余缓存任务。
- 同一个`ThreadHandle`不支持多线程并发访问。
- 同一个`ChannelHandle`不支持多线程并发访问。
- 返回`ASCCOMM_SUCCESS`仅表示发送请求处理成功且发送任务已生成，不表示任务已下发至硬件，也不表示对端已完成接收；任务是否已提交由Thread资源的提交策略决定。

## 调用示例

```cpp
// 前置条件：thread与channel为控制面传入的有效资源句柄，类型分别为ThreadHandle与ChannelHandle。
// src为本端已注册通信内存地址，类型为const void*，在发送完成前保持有效。
uint64_t len = 1024;
int32_t ret = asccomm_send_on_thread(thread, channel, src, len);
// 示例省略错误处理，实际调用时须检查ret。
```
