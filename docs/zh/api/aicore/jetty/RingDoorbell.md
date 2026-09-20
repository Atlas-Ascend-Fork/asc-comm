# RingDoorbell

## 功能说明

敲SQ门铃，发布已入队的WQE，支持SIMT构造/SIMD发布混合模式。

RingDoorbell将当前SQ头写入doorbell地址。混合模式下，SIMT侧完成WQE写入后由SIMD侧调用本接口发布；门铃写无法对另一个核发出的写入排序，调用前必须确保SIMT侧写入可见。

## 函数原型

SIMD路径：

```cpp
__aicore__ inline void RingDoorbell(...);
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
