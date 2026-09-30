# asccomm_local_copy_on_thread

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

提供本地内存拷贝功能，将src指向的长度为len的内存数据，拷贝到dst所指向的相同长度的内存中。

## 函数原型

```cpp
int32_t asccomm_local_copy_on_thread(ThreadHandle thread, void *dst, const void *src, uint64_t len)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为控制面构造并随AICPU Kernel参数下发的有效Thread资源句柄，当前实现要求该参数不能为0。<br>ThreadHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| dst | 输出 | 目的内存地址，Device内存。 |
| src | 输入 | 源内存地址，Device内存。 |
| len | 输入 | 数据长度（字节）。 |

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- dst和src必须为AICPU执行环境可直接访问的有效Device地址。
- len为0时当前不生成本地拷贝任务。
- 当前实现按最大4GiB分片提交拷贝任务；单片大小最终转换为`uint32_t`，恰好4GiB的边界行为需以运行时校验为准。
- 当前实现按最大4GiB分片提交拷贝任务；单片大小最终转换为`uint32_t`，恰好4GiB的边界行为需以运行时校验为准。
- 当前实现按最大4GiB分片提交拷贝任务；单片大小最终转换为`uint32_t`，恰好4GiB的边界行为需以运行时校验为准。

## 调用示例

```cpp
// thread为控制面下发的有效Thread资源句柄；src和dst为AICPU可直接访问的Device地址。
int32_t ret = asccomm_local_copy_on_thread(thread, dst, src, len);
```
