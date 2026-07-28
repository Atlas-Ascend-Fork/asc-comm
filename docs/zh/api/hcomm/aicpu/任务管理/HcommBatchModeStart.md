# HcommBatchModeStart

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：支持
<!-- end id3 -->
<!-- npu="310p" id4 -->
- Atlas 推理系列产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->

## 功能说明

头文件：[`#include "hcomm_primitives.h"`](https://gitcode.com/cann/hcomm/blob/master/include/hcomm_primitives.h)。

该接口用于开启批量模式。调用该接口后、调用[HcommBatchModeEnd](HcommBatchModeEnd.md)前发起的数据面接口调用（如[HcommLocalCopyOnThread](../本地数据操作/HcommLocalCopyOnThread.md)、[HcommWriteOnThread](../远端数据操作/HcommWriteOnThread.md)）将被暂存，不会立即提交执行。调用[HcommBatchModeEnd](HcommBatchModeEnd.md)时，这些操作将作为一个批次统一提交并触发执行。

## 函数原型

```cpp
int32_t HcommBatchModeStart(const char *batchTag)
```

## 参数说明

表1 参数说明

| 参数名称 | 输入/输出 | 含义 |
| --- | --- | --- |
| batchTag | 输入 | 批量任务标识符（可选）：<br>&nbsp;&nbsp;&bull; NULL或空字符串：表示该任务为临时批量任务，执行后不会被缓存；<br>&nbsp;&nbsp;&bull; 非空字符串：用于后续批量任务的标识与管理。<br>需要注意，在通信引擎为AI CPU+TS的场景下，当前暂未完全支持基于非空标识符的任务缓存管理功能。 |

## 返回值

表2 返回值说明

| 返回值类型 | 含义 |
| --- | --- |
| `int32_t` | 接口成功返回0，其他值表示失败。 |

## 约束说明

- 本接口和[HcommBatchModeEnd](HcommBatchModeEnd.md)必须成对调用，且需在同一线程中执行。

- 批量模式下暂存的操作需在调用[HcommBatchModeEnd](HcommBatchModeEnd.md)后才会提交执行。

<!-- npu="950" id6 -->
- 针对Ascend 950PR/Ascend 950DT，支持批量模式和立即执行模式（不调用批量接口）。
<!-- end id6 -->

<!-- npu="A3,910b" id7 -->
- 针对如下产品型号：

    <!-- npu="A3" id8 -->
    Atlas A3 训练系列产品/Atlas A3 推理系列产品；
    <!-- end id8 -->

    <!-- npu="910b" id9 -->
    Atlas A2 训练系列产品/Atlas A2 推理系列产品；
    <!-- end id9 -->

    必须使用批量模式。
<!-- end id7 -->

## 调用示例

```cpp
const char *tag = "";
// 启动批量模式（临时批量任务）。
HcommBatchModeStart(tag);

// 在批量模式中调用数据面接口不会立即执行。
// ...

// 结束批量模式并触发执行。
HcommBatchModeEnd(tag);
```

示例中目标接口返回0表示调用成功，返回其他值表示调用失败。
