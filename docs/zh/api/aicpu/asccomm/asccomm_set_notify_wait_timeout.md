# asccomm_set_notify_wait_timeout

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

设置通知等待的默认超时时间。该超时时间将应用于后续调用的[asccomm_channel_notify_wait_on_thread_with_default_timeout](./asccomm_channel_notify_wait_on_thread_with_default_timeout.md)和[asccomm_thread_notify_wait_on_thread_with_default_timeout](./asccomm_thread_notify_wait_on_thread_with_default_timeout.md)接口。

手动指定超时时间的[asccomm_channel_notify_wait_on_thread](./asccomm_channel_notify_wait_on_thread.md)和[asccomm_thread_notify_wait_on_thread](./asccomm_thread_notify_wait_on_thread.md)接口不受本接口影响。

## 函数原型

```cpp
int32_t asccomm_set_notify_wait_timeout(float timeOut)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | -------------- |
| timeOut | 输入 | 通知等待超时时间。<br>单位：秒。<br>取值说明：<br>  - 0：表示永久等待，不超时。<br>  - >0：具体的超时时间（秒）。<br>默认值：1836秒（约30分钟）。<br>当前只支持整数传入。 |

## 返回值说明

int32_t：接口成功返回0，其他失败。

## 约束说明

- 本接口设置的超时时间仅对`*WithDefaultTimeout`系列接口生效。
- 手动传入超时时间的 `asccomm_channel_notify_wait_on_thread`和`asccomm_thread_notify_wait_on_thread`不受本接口影响。
- 如果未调用本接口设置，默认超时时间为1836秒（约30分钟）。
- 在非AICPU模式下，如果未调用本接口设置默认超时时间，系统会自动在默认值基础上增加27秒的偏移量作为安全缓冲。
- 建议在数据面操作开始前设置默认超时时间。
- 当前只支持整数传入`timeOut`，不支持小数传入。

## 调用示例

```cpp
// 1. 设置默认超时时间（例如设置为10分钟）
float defaultTimeOut = 600;  // 600秒 = 10分钟
asccomm_set_notify_wait_timeout(defaultTimeOut);

// 2. 申请通信线程资源
CommEngine engine = COMM_ENGINE_AICPU_TS;
uint32_t threadNum = 1;
uint32_t notifyNumPerThread = 1;
ThreadHandle thread;
HcclComm comm;
HcclThreadAcquire(comm, engine, threadNum, notifyNumPerThread, &thread);

// 3. 申请通信通道资源
uint32_t channelNum = 1;
HcclChannelDesc channelDesc;
HcclChannelDescInit(&channelDesc, channelNum);
ChannelHandle channel;
HcclChannelAcquire(comm, engine, &channelDesc, channelNum, &channel);

// 4. 在Device侧记录通知
asccomm_channel_notify_record_on_thread(thread, channel, 0);

// 5. 数据面操作
// ...

// 6. 使用默认超时时间等待通知（无需手动传入超时参数）
asccomm_channel_notify_wait_on_thread_with_default_timeout(thread, channel, 0);

// 或者使用线程通知等待
asccomm_thread_notify_wait_on_thread_with_default_timeout(thread, 0);
```
