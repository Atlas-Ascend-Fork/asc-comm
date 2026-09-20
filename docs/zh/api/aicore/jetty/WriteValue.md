# WriteValue

## 功能说明

提交内联立即数写WQE，将`value`作为内联数据写入远端。

WriteValue将立即数内联携带在WQE中写入远端，不需要本端源buffer。

## 函数原型

SIMD路径：

```cpp
__aicore__ inline void WriteValue(...);
```

详见[Jetty](./Jetty.md)中的类定义。

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `value` | 输入 | 写入远端的立即数，T的大小需满足单个WQEBB的空闲空间。 |

## 返回值

无返回值（`void`接口）。

## 约束说明

- 仅支持Ascend 950平台的`COMM_PROTOCOL_UB_CTP`路径。
- 详见[Jetty](./Jetty.md)使用约束。
