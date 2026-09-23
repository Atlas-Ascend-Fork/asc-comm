# asccomm_aclrt_notify_record_on_thread

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

头文件路径：`"hcomm/asccomm_primitives.h"`。

基于acl接口创建的Notify发送同步信号，需与[asccomm_aclrt_notify_wait_on_thread](asccomm_aclrt_notify_wait_on_thread.md)配对使用。

## 函数原型

```cpp
int32_t asccomm_aclrt_notify_record_on_thread(ThreadHandle thread, uint64_t dstNotifyId)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 线程句柄，为通过`HcclThreadAcquire`接口（声明于`"hccl/hccl_res.h"`）获取到的threads。<br>ThreadHandle类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| dstNotifyId | 输入 | 同步信号ID，为通过aclrtGetNotifyId接口获取到的notifyId。 |

## 返回值说明

int32_t：接口成功返回0，其他失败。

## 约束说明

无

## 调用示例

```cpp
HcclComm comm;
CommEngine engine = COMM_ENGINE_CPU_TS;
aclrtStream streams[2];
ThreadHandle threads[2];
// 申请2条流，每条流2个Notify
aclrtCreateStream(&streams[0]);
aclrtCreateStream(&streams[1]);
HcclResult result = HcclThreadAcquireWithStream(comm, engine, streams[0], 2, &threads[0]);
result = HcclThreadAcquireWithStream(comm, engine, streams[1], 2, &threads[1]);
aclrtNotify notify;
uint64_t notifyId;
aclrtCreateNotify(&(notify), ACL_NOTIFY_DEFAULT);
aclrtGetNotifyId(notify, &(notifyId));
// 发送同步信号
asccomm_aclrt_notify_record_on_thread(threads[0], notifyId);
// 等待同步信号
uint32_t timeout = 1;
asccomm_aclrt_notify_wait_on_thread(threads[1], notifyId, timeout);
```
