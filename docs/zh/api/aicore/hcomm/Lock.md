# Lock

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

多个AI Core共享同一通信通道时，获取该通道的锁。`channel`有效时，如果锁已被持有，则等待直至成功获取锁，不会因锁竞争返回失败。

## 函数原型

```cpp
__aicore__ inline int32_t Lock(AscendC::ChannelHandle channel);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 获取锁成功。 |
| `-1` | `channel`为空或地址未对齐。锁竞争不会返回`-1`。 |

## 约束说明

- 仅支持AIV和`COMM_PROTOCOL_UB_CTP`、`COMM_PROTOCOL_ROCE`协议路径。
- 锁不可重入。释放锁前，不得对同一通道再次调用`Lock`。
- 每次成功调用`Lock`后，都必须由同一AI Core调用[Unlock](./Unlock.md)，错误退出路径也需要释放锁。
- 仅用于同步同一设备上共享同一通道的AI Core。

## 调用示例

Lock接口的调用示例如下。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512B
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void hcomm_lock_kernel(
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

    // 多个AI Core共享同一通道时，先获取通道锁
    hcomm.Lock(channel);

    // 获取锁成功后提交通信任务
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // 即使通信接口返回失败，也必须释放已成功获取的锁
    hcomm.Unlock(channel);
}
```
