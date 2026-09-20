# PublishSq

## 功能说明

写入128B DWQE窗口发布队列，SIMT延迟提交路径的组成部分。

PublishSq在SQ中写入一对NOP WQE并通过写DWQE窗口（doorbell地址前128B）发布队列。与AdvanceSq不同，PublishSq使硬件立即取指。

## 函数原型

SIMD路径：

```cpp
__aicore__ inline void PublishSq(...);
```

详见[Jetty](./Jetty.md)中的类定义。

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| - | - | 详见[Jetty](./Jetty.md)类定义。 |

## 返回值

无返回值（`void`接口）。

## 约束说明

- 仅支持Ascend 950平台的`COMM_PROTOCOL_UB_CTP`路径。
- 详见[Jetty](./Jetty.md)使用约束。
