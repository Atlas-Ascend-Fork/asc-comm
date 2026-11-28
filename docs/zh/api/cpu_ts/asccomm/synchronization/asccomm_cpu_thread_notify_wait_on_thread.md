# asccomm_cpu_thread_notify_wait_on_thread

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

头文件路径：`"cpu_ts/asccomm_cpu_primitives.h"`。

将本Host CPU线程上指定Notify的等待任务提交到`thread`对应的Host Stream后立即返回，常用于等待AI CPU执行器完成通信或计算任务后回送完成通知。

## 函数原型

```cpp
int32_t asccomm_cpu_thread_notify_wait_on_thread(uint64_t thread, uint32_t notifyIdx, uint32_t timeOut)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | Host CPU线程对象地址。控制面创建CPU_TS线程资源并导出后传入；调用时不能为0。类型为`uint64_t`。 |
| notifyIdx | 输入 | 本地Notify索引。取值范围为`[0, thread资源中的Notify数量)`。 |
| timeOut | 输入 | Notify等待任务的超时时间，单位：秒。必须大于0；传0时等待任务执行时会立即超时返回，不表示永久等待。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `0` | Notify等待任务已成功提交到Host Stream；不表示指定Notify已收到同步信号，也不表示等待任务已经执行完成。 |
| `1` | `thread`、`notifyIdx`或Notify资源参数不合法，或线程对象布局校验失败。 |
| `2` | 运行环境未提供底层Notify等待接口，本接口在提交等待任务前直接返回。 |
| 其他值 | 底层Notify等待接口提交等待任务失败时返回的原始错误码，本接口原样透传。 |

## 约束说明

- `thread`必须来自控制面创建的有效CPU_TS线程资源。
- `notifyIdx`必须小于本端线程资源中的Notify数量，且对应Notify资源必须有效。
- `timeOut`必须大于0；传0时等待任务执行时会立即超时返回，不表示永久等待。
- `timeOut`仅作用于Host Stream上执行的Notify等待任务，本接口不会同步等待该任务执行完成。
- 本接口需与[asccomm_cpu_thread_notify_record_on_thread](asccomm_cpu_thread_notify_record_on_thread.md)配合使用。AI CPU侧应使用AICPU_TS Asccomm的[asccomm_thread_notify_record_on_thread](../../../aicpu_ts/asccomm/synchronization_and_barrier/asccomm_thread_notify_record_on_thread.md)向Host CPU线程回送通知。
- 调用方必须保证线程资源和Notify资源在等待任务完成前处于有效生命周期内。
- 同一个线程对象不支持多线程并发调用本接口；任务的执行顺序由`thread`绑定的Host Stream保证。

## 调用示例

```cpp
// thread为控制面导出的Host CPU线程对象地址；
// notifyIdx为AI CPU完成通知使用的本地Notify索引；
// timeout单位为秒，必须大于0。
constexpr uint32_t timeout = 1836;
int32_t ret = asccomm_cpu_thread_notify_wait_on_thread(thread, notifyIdx, timeout);
if (ret != 0) {
    // 按上层错误处理流程处理提交失败。
}
```

完整调用场景参见[aicpu_ts_direct](../../../../../../examples/aicpu_ts/aicpu_ts_direct/README.md)目录下的自定义AI CPU算子样例。
