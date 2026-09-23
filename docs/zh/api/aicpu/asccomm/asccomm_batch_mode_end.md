# asccomm_batch_mode_end

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
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="910" id5 -->
- Atlas 训练系列产品：不支持
<!-- end id5 -->
<!-- npu="310p" id6 -->
- Atlas 推理系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件路径：`"hcomm/asccomm_primitives.h"`。

该接口用于提交并触发批量模式下缓存的所有操作的执行。所有在[asccomm_batch_mode_start](asccomm_batch_mode_start.md)和[asccomm_batch_mode_end](asccomm_batch_mode_end.md)之间的数据面接口调用操作将在此时统一执行。

## 函数原型

```cpp
int32_t asccomm_batch_mode_end(const char *batchTag)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| batchTag | 输入 | 批量任务标识符，需要与[asccomm_batch_mode_start](asccomm_batch_mode_start.md)中传入的batchTag保持一致。 |

## 返回值说明

int32_t：接口成功返回0，其他失败。

## 约束说明

[asccomm_batch_mode_start](asccomm_batch_mode_start.md)和[asccomm_batch_mode_end](asccomm_batch_mode_end.md)必须成对调用，且需要在同一线程中执行。

<!-- npu="950" id7 -->
仅Ascend 950PR/Ascend 950DT系列产品支持批量模式和立即执行模式（不调用批量接口），其他产品必须使用批量模式。
<!-- end id7 -->

## 调用示例

```cpp
char *tag = "";
// 启动批量模式（临时批量任务）
asccomm_batch_mode_start(tag);

// 在批量模式中调用数据面接口不会立即执行
// ...

// 结束批量模式并触发执行
asccomm_batch_mode_end(tag);
```
