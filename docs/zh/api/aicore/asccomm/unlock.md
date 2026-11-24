# unlock

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

释放通过[lock](./lock.md)获取的通信通道锁。

## 函数原型

```cpp
__aicore__ inline int32_t unlock(ChannelHandle channel);
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
| `0` | 释放锁成功。 |
| `-1` | 锁未被持有（锁状态异常）；`COMM_PROTOCOL_ROCE`协议下`channel`为空或地址未对齐时同样返回该值。 |

`COMM_PROTOCOL_UB_CTP`协议下`channel`为空或地址未对齐时，调试模式下触发Trap，不返回错误码。

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- 仅在[lock](./lock.md)成功后调用`unlock`，并由获取锁的同一AI Core释放。
- 同一次锁获取不得重复调用`unlock`。
- 仅用于同步同一设备上共享同一通道的AI Core。

## 调用示例

unlock接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[07_asccomm_roce_normal_write_read](../../../../../examples/aicore/asccomm/07_asccomm_roce_normal_write_read/README.md)。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t ASCCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void unlock_kernel(
    uint64_t channel, GM_ADDR local_buffer, GM_ADDR remote_buffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[ASCCOMM_BUF_SIZE];

    // 创建asccomm对象并初始化UB临时工作区
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    asccomm.init(asccomm_local, ASCCOMM_BUF_SIZE);

    // 通过Lock获取通道锁
    asccomm.lock(channel);

    // 锁内提交通信任务
    asccomm.write_nbi(channel, remote_buffer, local_buffer, BYTES);

    // 通信任务完成后，由获取锁的同一AI Core释放锁
    asccomm.unlock(channel);
}
```
