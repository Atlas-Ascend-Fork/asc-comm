# CCU接口简介

CCU API分为Host控制面和CCU kernel数据面两类。控制面负责通信域、通信实例、片上资源
和任务下发；数据面负责参数加载、资源使用、数据搬运和同步。

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

- [Host控制面API](host/README.md)
- [编译接口](compiler.md)

## 头文件

Host侧使用`<ccu_runtime.h>`：

```cpp
#include <ccu_runtime.h>
```

CCU kernel侧使用`<ccu_intrinsics.h>`：

```cpp
#include <ccu_intrinsics.h>
```

以上头文件随CCU开发环境发布，安装后位于`tools/ccuc/include`目录。

## 版本与容量

```cpp
#define CCU_VAR_MAX              3072
#define CCU_EVENT_MAX            1024
#define CCU_BUF_MAX              1536
#define CCU_CHANNEL_MAX          128
#define CCU_CCUA_COUNT           4
#define CCU_RESOURCE_SPACE_SIZE  (72ULL * 1024 * 1024)
```

容量宏描述当前Ascend 950PR&950DT系列产品的资源上限。
