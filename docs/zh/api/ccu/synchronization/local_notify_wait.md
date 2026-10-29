# local_notify_wait

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

头文件为：`#include "ccu/hcomm/ccu_primitives.hpp"`

在CCU kernel内阻塞等待同die内跨kernel同步信号的消费者侧接口，以字符串tag标识配对。硬件执行到此处会阻塞，直到对应bit被置位后继续。

> [!NOTE]说明
> 本接口在C++层实现为`AscendC::ccu::event_wait(const char* notify_tag, uint16_t mask)`重载，与`event_wait(event, mask)`共用同一函数名，通过入参类型区分。适用于同一die内不同kernel之间的顺序同步，不适用于跨die或跨rank场景，跨die场景请使用[notify_wait](notify_wait.md)。

## 函数原型

```cpp
// local_notify_wait 对应 event_wait 的 const char* 重载
CcuResult event_wait(const char* notify_tag, uint16_t mask = 1);
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| notify_tag | 输入 | 字符串tag，必须与生产侧`local_notify_record`的`notify_tag`完全一致以完成配对。不可为空指针。约束同生产侧，需为字符串字面量或kernel内的`static`存储。 |
| mask | 输入 | 16位事件掩码，指定要等待的bit。默认值为`1`。必须与生产侧`local_notify_record`的`mask`一致。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PTR` | `notify_tag`为空指针，或接口在编译阶段之外被调用。 |

## 约束说明

- `local_notify_wait`可在硬件loop body内调用。
- `notify_tag`必须在Kernel函数体执行期间保持有效，约束与`local_notify_record`一致。
- 调用前必须已存在对应的`local_notify_record`在某个先序位置，否则将永久阻塞导致硬件级死锁。
- 等待的`mask`必须与生产侧`local_notify_record`传入的`mask`一致。

## 调用示例

> 下面示例中调用的`event_wait("phase1_done", 0x1)`即本文档所述的`local_notify_wait`——在C++层它与[event_wait](event_wait.md)（`event`对象重载）共用同一函数名，通过入参类型（`const char*` vs `event`）区分。

```cpp
// 为简化示例，省略返回值接收与错误处理
using namespace AscendC::ccu;

// 场景：同die内，phase_consumer_kernel等待phase_producer_kernel完成阶段1后才开始阶段2
// core 1 上的CCU kernel函数体内
CcuResult phase_consumer_kernel(ccu_kernel_arg arg) {
    // 等待phase_producer_kernel发出"phase1_done"信号的bit0（即local_notify_wait语义）
    event_wait("phase1_done", 0x1);

    // 此后可安全使用phase_producer_kernel阶段1的结果
    // ... 阶段2操作 ...
    return CCU_SUCCESS;
}
```
