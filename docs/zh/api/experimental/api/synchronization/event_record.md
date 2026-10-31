# Event记录

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

- `__ccu_event_set`：置位本地event的指定signal mask。
- `__ccu_event_clear`：清除本地event的指定signal mask。

异步搬运接口会在完成时自动置位完成event，通常不需要手工set。`clear`只用于明确的事件复用场景。

## 函数原型

```cpp
void __ccu_event_set(__ccu_event_t event, uint16_t mask);
void __ccu_event_clear(__ccu_event_t event, uint16_t mask);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `event` | 输入 | `__ccu_event_t` | 本地event句柄；由kernel声明后自动分配 |
| `mask` | 输入 | `uint16_t` | 16位signal mask，必须是编译期常量；`0x0`表示不操作任何signal |

## 返回值说明

接口返回`void`。非法event使用或非法mask由编译器诊断。

## 约束说明

- `mask`必须是编译期常量。
- event必须由当前kernel声明或由当前kernel可访问的资源提供。
- event是kernel内声明的逻辑资源。
- `event_set`不等待对端可见；跨rank通知使用notify接口。

## 调用示例

```cpp
// 1. 声明本地 event。
__ccu_event_t ready;

// 2. 置位 bit0，表示本地条件已满足。
__ccu_event_set(ready, 1);

// 3. 等待并消费同一 event 的 bit0。
__ccu_event_wait(ready, 1);

// 4. 如需复用该 event，先清除已消费的 signal。
__ccu_event_clear(ready, 1);
```
