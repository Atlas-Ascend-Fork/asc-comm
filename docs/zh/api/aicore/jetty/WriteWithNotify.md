# WriteWithNotify

## 功能说明

提交URMA写WQE，并在完成时向远端通知地址写入通知值。

WriteWithNotify在普通写WQE基础上附加完成通知：硬件完成写入后，将`notifyValue`写入`notifyAddr`。

## 函数原型

SIMD路径：

```cpp
__aicore__ inline void WriteWithNotify(...);
```

详见[Jetty](./Jetty.md)中的类定义。

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `notifyAddr` | 输出 | 远端通知GM绝对地址。 |
| `notifyValue` | 输入 | 完成时写入`notifyAddr`的64位值。 |

## 返回值

无返回值（`void`接口）。

## 约束说明

- 仅支持Ascend 950平台的`COMM_PROTOCOL_UB_CTP`路径。
- 详见[Jetty](./Jetty.md)使用约束。
