# asccomm_batch_mode_end

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

该接口用于结束[asccomm_batch_mode_start](asccomm_batch_mode_start.md)开启的批量提交模式。调用本接口时，当前AICPU执行线程在该模式下缓存的通信任务会被统一提交。接口成功返回表示提交动作成功，不表示硬件通信任务已经全部完成。

## 函数原型

```cpp
int32_t asccomm_batch_mode_end(const char *batchTag)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| batchTag | 输入 | 保留参数。当前实现不读取、不校验该参数，可传入NULL。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 结束批量提交模式并提交缓存任务成功；没有待提交任务时也返回此值。 |
| `ASCCOMM_E_PTR` | Thread资源、本地任务缓存指针为空，或提交缓存任务时所需地址为空。 |
| `ASCCOMM_E_PARA` | Thread资源、通信任务提交队列或上下文参数不合法。 |
| `ASCCOMM_E_TIMEOUT` | 提交缓存任务时，通信队列空间不足且等待超时。 |
| `ASCCOMM_E_INTERNAL` | 提交缓存任务时发生内部异常。 |

## 约束说明

- Start与End应成对调用，且Start、End以及两者之间提交的数据面通信任务必须在同一AICPU执行线程中执行。
- End只提交当前AICPU执行线程在批量模式下缓存的尚未提交任务。
- 当前实现中，内部缓存满时会提前提交；当前缓存容量为128。
- 批量模式期间没有需要提交的缓存任务时，本接口仍返回`ASCCOMM_SUCCESS`，例如未调用数据面接口或缓存任务已提前提交。
- 本接口成功返回表示缓存任务提交成功，不表示硬件通信任务已经全部完成。

## 调用示例

```cpp
// batchTag为保留参数，当前实现不读取；示例传入nullptr。
const char *batchTag = nullptr;
// 开启批量模式
int32_t ret = asccomm_batch_mode_start(batchTag);

// 批量模式期间，通信任务会先缓存
// ...

// 结束批量模式并统一提交缓存任务
ret = asccomm_batch_mode_end(batchTag);
(void)ret; // 示例省略错误处理，实际调用时应检查返回值
```
