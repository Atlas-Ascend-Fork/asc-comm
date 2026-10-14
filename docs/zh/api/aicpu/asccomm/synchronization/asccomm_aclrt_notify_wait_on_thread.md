# asccomm_aclrt_notify_wait_on_thread

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

等待acl接口创建的Notify收到同步信号，须与[asccomm_aclrt_notify_record_on_thread](asccomm_aclrt_notify_record_on_thread.md)配对使用。

## 函数原型

```cpp
int32_t asccomm_aclrt_notify_wait_on_thread(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄。控制面创建Thread资源后，将该句柄作为AICPU Kernel参数传入；调用时不能为0。类型为`ThreadHandle`，包含`"aicpu/hcomm/asccomm_primitives.h"`后即可使用。 |
| notifyId | 输入 | 目标Notify的ID，由ACL运行时的`aclrtGetNotifyId`接口获取。<br>当前实现按32位Notify ID下发，取值不能超过`UINT32_MAX`。 |
| timeOut | 输入 | 等待超时时间，单位：秒。<br>0表示永久等待，大于0表示指定超时时间。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `ASCCOMM_SUCCESS`（0） | 通知等待任务处理成功。 |
| `ASCCOMM_E_PTR` | Thread句柄为0，或Thread资源、本地任务缓存指针为空。 |
| `ASCCOMM_E_PARA` | Thread资源或通信任务提交队列参数不合法。 |
| `ASCCOMM_E_TIMEOUT` | 触发任务提交时，等待通信任务提交队列空间超时；不表示Notify等待任务超时。 |
| `ASCCOMM_E_INTERNAL` | 任务生成、驱动查询或配置、本地任务缓存操作等内部执行失败。 |

## 约束说明

- 本接口需与[asccomm_aclrt_notify_record_on_thread](asccomm_aclrt_notify_record_on_thread.md)配合使用。
- 通知等待任务提交时机与Thread资源的提交策略一致；普通模式可立即提交，批量模式或缓存满时按当前实现提交。
- `timeOut`仅作用于Notify等待任务本身。本接口提交等待任务后立即返回，Notify等待超时不通过返回值反馈。

## 调用示例

```cpp
// thread为控制面传入的有效ThreadHandle资源句柄；notifyId为ACL运行时Notify资源ID。
uint32_t timeout = 1800;  // 单位：秒
int32_t ret = asccomm_aclrt_notify_wait_on_thread(thread, notifyId, timeout);
```
