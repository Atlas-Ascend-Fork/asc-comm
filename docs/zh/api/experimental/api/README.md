# CCU接口简介

CCU API分为Host控制面和CCU kernel数据面两类。控制面负责通信域、context、instance、
buffer和任务下发；数据面负责参数加载、资源使用、数据搬运和同步。

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

## 数据面接口分类

- [参数加载/存储](arg_load_store/README.md)
- [数据搬运](data_movement/README.md)
- [资源申请与操作](resource_allocation_operation/README.md)
- [同步](synchronization/README.md)

## Host控制面

- [Host Runtime API](host/README.md)
- [编译接口](compiler.md)

## 头文件

Host侧：

```cpp
#include <ccu_runtime.h>
```

CCU kernel侧：

```cpp
#include <ccu_intrinsics.h>
```

CCU kernel由`ccuc`编译。Host侧还需要链接`libccu_runtime.so.1`，并由部署环境提供
HCOMM public header、library和通信域创建能力。

## 版本与容量

```cpp
#define CCU_INTRINSICS_VERSION 1
#define CCU_RUNTIME_API_VERSION 1

#define CCU_VAR_MAX              3072
#define CCU_EVENT_MAX            1024
#define CCU_BUF_MAX              1536
#define CCU_CHANNEL_MAX          128
#define CCU_CCUA_COUNT           4
#define CCU_RESOURCE_SPACE_SIZE  (72ULL * 1024 * 1024)
```

容量宏描述当前Ascend 950PR&950DT系列产品目标的上限。资源由编译器和运行时统一分配；用户代码使用逻辑资源，不直接推断或填写资源编号。
