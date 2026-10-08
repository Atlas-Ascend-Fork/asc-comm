# Commit

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

通知指定普通通道上已准备的通信任务可以开始执行。通常在调用普通`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`AtomicFAA`或`AtomicCAS`时将`commit`设置为`false`后显式调用。

该接口只接受`ChannelHandle`。BatchHandle中的任务需要通过[BatchCommit](./BatchCommit.md)提交。

## 函数原型

```cpp
template <pipe_t pipe = PIPE_S>
__aicore__ inline int32_t Commit(AscendC::ChannelHandle channel);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `pipe` | commit使用的pipe，默认`PIPE_S`。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通通信通道句柄。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 调用前需要通过`Init`为普通接口提供临时工作区。
- 若`commit`模板参数设为`false`，连续积攒的任务槽位不得超过通道的任务提交容量；需在容量耗尽前通过`Commit`或自动commit提交，否则后续任务提交将失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应生成完成记录。对于URMA任务，`config`是传入各通信接口的任务配置，其`cqe`字段用于控制该任务是否生成完成记录。中间任务的`config.cqe`设为0，最后一个任务设为1。
- BatchHandle中的任务只能通过`BatchCommit`提交，不能在同一通道上混用普通`Commit`和`BatchCommit`。

## 调用示例

Commit接口的调用示例如下。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void hcomm_commit_kernel(
    __gm__ void* channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备UB临时工作区
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // 提交任务时设置commit = false，只组装任务不立即提交
    hcomm.WriteNbi<false>(channel, remoteBuffer, localBuffer, BYTES);

    // 显式提交通道上已积攒的任务
    hcomm.Commit(channel);

    // 阻塞等待已提交任务完成
    hcomm.Drain(channel);
}
```
