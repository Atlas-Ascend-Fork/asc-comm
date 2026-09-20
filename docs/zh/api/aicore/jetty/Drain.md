# Drain

## 功能说明

轮询CQ，等待已提交的WQE全部完成。

Drain持续消费CQE直到已提交的WQE全部完成，并通过CQ门铃推进完成队列。

## 函数原型

SIMD路径：

```cpp
__aicore__ inline void Drain(...);
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
