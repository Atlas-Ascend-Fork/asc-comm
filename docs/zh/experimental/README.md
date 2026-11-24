# CCU用户文档

本文档介绍CCU kernel的编写、编译和运行方法。

## 文档目录

| 目录 | 内容 |
| --- | --- |
| [api](api/README.md) | CCU API和编译接口 |
| [guide](guide/README.md) | 快速开始和样例说明 |

## 阅读顺序

1. [快速开始](guide/quick_start.md)
2. [编译接口](api/compiler.md)
3. [Host控制面API](api/host/README.md)
4. [CCU数据面API](api/README.md)
5. [集合通信样例](guide/collective_example.md)

## 适用范围

- Host控制面与CCU数据面均由`ccuc`编译；源码可分别包含`<ccu_runtime.h>`和
  `<ccu_intrinsics.h>`，可省略显式包含。
- 通信域、通信channel、通信Buffer和内存token由HCOMM接口管理。
- CCU位于一个设备的IO Die上，不能跨Die使用另一个IO Die的网络设备进行通信。
- 当前单个Die仅支持一个任务，kernel启动时`HcomCcuSchd.missionNum`仅支持1。
