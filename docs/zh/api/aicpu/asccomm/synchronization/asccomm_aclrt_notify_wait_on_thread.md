# asccomm_aclrt_notify_wait_on_thread

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：不支持
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

基于acl接口创建的Notify等待同步信号，须与[asccomm_aclrt_notify_record_on_thread](asccomm_aclrt_notify_record_on_thread.md)配对使用。

## 函数原型

```cpp
int32_t asccomm_aclrt_notify_wait_on_thread(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为控制面构造并随AICPU Kernel参数下发的有效Thread资源句柄，当前实现要求该参数不能为0。<br>ThreadHandle类型的定义请参见`"asccomm/hcomm_res_defs.h"`。 |
| notifyId | 输入 | 同步信号ID，为通过aclrtGetNotifyId接口获取到的notifyId。 |
| timeOut | 输入 | 超时时间，单位：秒。<br>  - 0：表示永久等待。<br>  - >0：配置的具体超时时间。 |

## 返回值说明

int32_t：成功返回`ASCCOMM_SUCCESS`（0），失败返回`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_INTERNAL`等错误码。

## 约束说明

- `notifyId`会被转换为`uint32_t`使用，调用方须确保其可无损转换。

## 调用示例

```cpp
uint32_t timeout = 1800;  // 单位：秒
int32_t ret = asccomm_aclrt_notify_wait_on_thread(thread, notifyId, timeout);
```
