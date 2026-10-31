# Event等待

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

等待本地event中`mask`指定的signal全部置位。条件满足后，`__ccu_event_wait`会清除这些signal，并建立后续指令对本次等待的依赖。

## 函数原型

```cpp
void __ccu_event_wait(__ccu_event_t event, uint16_t mask = 1);
```

## 参数说明

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `event` | 输入 | `__ccu_event_t` | 本地event句柄 |
| `mask` | 输入 | `uint16_t` | 等待的16位signal mask，默认`1`；必须是编译期常量 |

## 返回值说明

接口返回`void`。event使用非法或mask非法由编译器诊断。

## 约束说明

- `mask`必须是16位编译期常量。
- `0x0`表示不等待任何signal。
- event必须由当前kernel声明或由当前kernel可访问的资源提供。
- 等待成功后，指定signal会被清除。

## 调用示例

```cpp
// 1. 声明完成事件。
__ccu_event_t copy_done;

// 2. 发起异步搬运，并指定完成时置位 bit0。
__ccu_local_copy_nb(output, input, bytes, copy_done, 1);

// 3. 等待搬运完成；满足后 bit0 会被清除。
__ccu_event_wait(copy_done, 1);
```
