# asccomm_cpu_thread_notify_record_on_thread

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

向目标AI CPU线程的Notify提交记录任务，用于Host CPU向AI CPU发送启动或门铃通知。接口把记录任务提交到`thread`对应的Host Stream，由运行时在该流上执行记录动作。

## 函数原型

```cpp
int32_t asccomm_cpu_thread_notify_record_on_thread(uint64_t thread, uint64_t dstThread, uint32_t dstNotifyIdx)
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| thread | 输入 | Host CPU线程对象地址。控制面创建CPU_TS线程资源并导出后传入；调用时不能为0。类型为`uint64_t`。 |
| dstThread | 输入 | 目标AI CPU线程对象地址。控制面创建AI CPU线程资源并导出到CPU_TS引擎后传入；调用时不能为0。类型为`uint64_t`。 |
| dstNotifyIdx | 输入 | 目标AI CPU线程的Notify索引。取值范围为`[0, dstThread资源中的Notify数量)`。 |

## 返回值说明

返回值为`int32_t`，取值含义如下。

| 返回值 | 说明 |
| --- | --- |
| `0` | Notify记录任务已成功提交到Host Stream；不表示目标Notify已经收到同步信号。 |
| `1` | `thread`、`dstThread`或Notify资源参数不合法，或线程对象布局校验失败。 |
| `2` | 运行环境未提供底层Notify记录接口，本接口在提交任务前直接返回。 |
| 其他值 | 底层Notify记录接口提交任务失败时返回的原始错误码，本接口原样透传。 |

## 约束说明

- `thread`必须来自控制面创建的有效CPU_TS线程资源，`dstThread`必须来自已导出到CPU_TS引擎的有效AI CPU线程资源。
- `dstNotifyIdx`必须小于目标AI CPU线程资源中的Notify数量，且对应Notify资源必须有效。
- 调用方必须保证线程资源和Notify资源在任务完成前处于有效生命周期内。
- 本接口只提交Notify记录任务，不能替代Host与AI CPU之间的数据搬运或流同步。
- 同一个线程对象不支持多线程并发调用本接口；任务的执行顺序由`thread`绑定的Host Stream保证。

## 调用示例

```cpp
// thread为控制面导出的Host CPU线程对象地址；
// dstThread为导出到CPU_TS引擎的AI CPU线程对象地址；
// dstNotifyIdx为AI CPU内核等待的Notify索引。
int32_t ret = asccomm_cpu_thread_notify_record_on_thread(thread, dstThread, dstNotifyIdx);
if (ret != 0) {
    // 按上层错误处理流程处理提交失败。
}
```

完整调用场景参见[aicpu_ts_direct](../../../../../../examples/aicpu_ts/aicpu_ts_direct/README.md)目录下的自定义AI CPU算子样例。
