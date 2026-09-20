# AdvanceSq

## 功能说明

推进SQ头并发布到device侧head地址，延迟提交路径的组成部分。

AdvanceSq将SQ头推进指定数量的WQEBB并把打包后的head写入GM。SIMT路径同时完成终止WQE标记（FINAL flags与必要的NOP填充）。AdvanceSq不发布队列，必须随后调用PublishSq（SIMT）或RingDoorbell（SIMD）。

## 函数原型

SIMD路径：

```cpp
__aicore__ inline void AdvanceSq(...);
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
