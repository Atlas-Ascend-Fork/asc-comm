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

头文件路径：`"asccomm/asccomm_primitives.h"`。

该接口用于关闭当前执行线程的AICPU数据面批量提交模式，并统一提交批量模式期间已登记Thread的本地SQE。接口成功返回表示提交动作成功，不表示所有硬件通信任务已经完成。

## 函数原型

```cpp
int32_t asccomm_batch_mode_end(const char *batchTag)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| batchTag | 输入 | 保留参数。当前实现不读取、不校验该参数，可传入NULL。 |

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- Start与End应成对调用，且需在同一执行线程中调用。
- 批量期间没有登记Thread时，本接口可能直接返回成功。
- 本接口只负责提交SQE，不等待所有通信任务完成。

## 调用示例

```cpp
const char *batchTag = nullptr;
// 启动批量模式
asccomm_batch_mode_start(batchTag);

// 在批量模式中调用数据面接口不会立即执行
// ...

// 结束批量模式并触发执行
asccomm_batch_mode_end(batchTag);
```
