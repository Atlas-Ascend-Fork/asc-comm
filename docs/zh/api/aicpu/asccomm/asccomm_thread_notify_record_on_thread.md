# asccomm_thread_notify_record_on_thread

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

向其他Thread发送同步信号，主要用于多Thread之间的同步等待场景。

## 函数原型

```cpp
int32_t asccomm_thread_notify_record_on_thread(ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | 通信线程句柄，为通过`HcclThreadAcquire`接口（声明于`"hccl/hccl_res.h"`）获取到的threads。<br>ThreadHandle类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| dstThread | 输入 | 目标通信线程句柄，为通过`HcclThreadAcquire`接口（声明于`"hccl/hccl_res.h"`）获取到的threads。<br>ThreadHandle类型的定义请参见`"hcomm/hcomm_primitives.h"`。 |
| dstNotifyIdx | 输入 | 目标Notify索引。<br>取值范围为：［0，`HcclThreadAcquire`接口传入的notifyNumPerThread参数的值）。 |

## 返回值说明

int32_t：接口成功返回0，其他失败。

## 约束说明

该接口需要配合[asccomm_thread_notify_wait_on_thread](asccomm_thread_notify_wait_on_thread.md)使用。

<!-- npu="950" id7 -->
在Ascend 950PR/Ascend 950DT系列产品上，仅支持AICPU_TS模式下、在Device侧调用该接口。
<!-- end id7 -->

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
uint32_t notifyIdx = 0;
// 发送同步信号
asccomm_thread_notify_record_on_thread(threads[0], threads[1], notifyIdx);
uint32_t timeout = 1;
// 等待同步信号
asccomm_thread_notify_wait_on_thread(threads[1], notifyIdx, timeout);
```

<!-- npu="950" id8 -->
在Ascend 950PR/Ascend 950DT系列产品上，该函数需要编译到Device侧使用：

```cpp
HcclComm comm;
CommEngine engine = COMM_ENGINE_AICPU_TS;
ThreadHandle threads[2];
uint32_t notifyNumPerThread = 2;
HcclThreadAcquire(comm, engine, 1, notifyNumPerThread, &threads[0]);
HcclThreadAcquire(comm, engine, 1, notifyNumPerThread, &threads[1]);

// 其余资源申请
// 拷贝参数并Launch Kernel

// Device侧算法编排
uint32_t notifyIdx = 0;
// 发送同步信号
asccomm_thread_notify_record_on_thread(threads[0], threads[1], notifyIdx);
uint32_t timeout = 1;
// 等待同步信号
asccomm_thread_notify_wait_on_thread(threads[1], notifyIdx, timeout);
```
<!-- end id8 -->
