# asccomm_batch_mode_start

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

该接口用于开启批量模式，在[asccomm_batch_mode_start](asccomm_batch_mode_start.md)和[asccomm_batch_mode_end](asccomm_batch_mode_end.md)之间的所有数据面接口调用（如asccomm_local_copy_on_thread、asccomm_write_on_thread等）将被缓存，不会立即执行。所有操作将在调用[asccomm_batch_mode_end](asccomm_batch_mode_end.md)时统一提交并执行。

## 函数原型

```cpp
int32_t asccomm_batch_mode_start(const char *batchTag)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| batchTag | 输入 | 批量任务标识符（可选）。若传入NULL或空字符串，表示该任务为临时批量任务，执行后不会被缓存；若传入非空字符串，则用于后续批量任务的标识与管理。<br>需要注意，在通信引擎为AI CPU+TS的场景下，当前暂未完全支持基于非空标识符的任务缓存管理功能。 |

## 返回值说明

int32_t：接口成功返回0，其他失败。

## 约束说明

1. [asccomm_batch_mode_start](asccomm_batch_mode_start.md)和[asccomm_batch_mode_end](asccomm_batch_mode_end.md)必须成对调用，且需在同一线程中执行。
2. 批量模式下缓存的操作需在[asccomm_batch_mode_end](asccomm_batch_mode_end.md)调用后才会实际执行。
3. 仅Ascend 950PR/Ascend 950DT系列产品支持批量模式和立即执行模式（不调用批量接口），其他产品必须使用批量模式。

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
