# asccomm_batch_mode_start

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

## 功能说明

头文件路径：`"aicpu/hcomm/asccomm_primitives.h"`。

该接口用于开启当前AICPU执行线程的批量提交模式。批量模式期间，需要下发的数据面通信任务会先缓存，调用[asccomm_batch_mode_end](asccomm_batch_mode_end.md)时统一提交。

## 函数原型

```cpp
int32_t asccomm_batch_mode_start(const char *batchTag)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| batchTag | 输入 | 保留参数。当前实现不读取、不校验该参数，可传入NULL。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 开启批量提交模式成功。 |
| `ASCCOMM_E_INTERNAL` | 内部执行异常。 |

## 约束说明

- Start与End应成对调用，且Start、End以及两者之间提交的数据面通信任务必须在同一AICPU执行线程中执行。
- 批量状态仅作用于当前AICPU执行线程；End只提交当前执行线程在批量模式下缓存的任务。
- 当前实现中，内部缓存满时会提前提交；当前缓存容量为128。
- 本接口成功返回表示批量模式设置成功，不表示数据面任务已经提交或完成。

## 调用示例

```cpp
// batchTag为保留参数，当前实现不读取；示例传入nullptr。
const char *batchTag = nullptr;
// 开启批量模式
int32_t ret = asccomm_batch_mode_start(batchTag);

// 批量模式期间，通信任务会先缓存
// ...

// 关闭批量模式并统一提交缓存任务
ret = asccomm_batch_mode_end(batchTag);
(void)ret; // 示例省略错误处理，实际调用时应检查返回值
```
