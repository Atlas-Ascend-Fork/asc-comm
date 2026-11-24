# lock

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

头文件路径：`"asccomm/asccomm.h"`。

多个AI Core共享同一通信通道时，获取该通道的锁。`channel`有效时，如果锁已被持有，则等待直至成功获取锁，不会因锁竞争返回失败。

## 函数原型

```cpp
__aicore__ inline int32_t lock(ChannelHandle channel);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |

## 返回值说明

**表2** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 获取锁成功。锁竞争时自旋等待，直至获取成功后返回。 |
| `-1` | `COMM_PROTOCOL_ROCE`协议下`channel`为空或地址未对齐。 |

`COMM_PROTOCOL_UB_CTP`协议下`channel`为空或地址未对齐时，调试模式下触发Trap，不返回错误码；锁竞争不会返回`-1`。

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- 锁不可重入。释放锁前，不得对同一通道再次调用`lock`。
- 每次成功调用`lock`后，都必须由同一AI Core调用[unlock](./unlock.md)，错误退出路径也需要释放锁。
- 仅用于同步同一设备上共享同一通道的AI Core。

## 调用示例

lock接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[07_asccomm_roce_normal_write_read](../../../../../examples/aicore/asccomm/07_asccomm_roce_normal_write_read/README.md)。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t ASCCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void lock_kernel(
    uint64_t channel, GM_ADDR local_buffer, GM_ADDR remote_buffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[ASCCOMM_BUF_SIZE];

    // 创建asccomm对象并初始化UB临时工作区
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    asccomm.init(asccomm_local, ASCCOMM_BUF_SIZE);

    // 多个AI Core共享同一通道时，先获取通道锁
    asccomm.lock(channel);

    // 获取锁成功后提交通信任务
    asccomm.write_nbi(channel, remote_buffer, local_buffer, BYTES);

    // 即使通信接口返回失败，也必须释放已成功获取的锁
    asccomm.unlock(channel);
}
```
