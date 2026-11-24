# drain

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

头文件路径：`"asccomm/asccomm_simt.h"`。

等待已提交的通信任务完成。接口提供普通`channel`重载和批量`batch_handle`重载：普通重载阻塞等待普通通道上已提交任务完成；批量重载等待批量句柄已提交任务生成的完成记录。

## 函数原型

普通接口：

```cpp
template <auto pipe = 0>
__simt_callee__ inline int32_t drain(ChannelHandle channel);
```

批量接口：

```cpp
template <auto pipe = 0>
__simt_callee__ inline int32_t drain(ubc_ctp_batch_handle& batch_handle);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `pipe` | 该参数在SIMT路径下不使用，仅为兼容SIMD路径接口形态而保留，默认值为0。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batch_handle` | 输入/输出 | [make_batch_handle](./simt_make_batch_handle.md)创建的批量句柄。 |

## 返回值说明

**表3** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 等待成功。 |
| `-1` | 等待失败。 |

## 约束说明

- 仅支持`COMM_PROTOCOL_UB_CTP`协议。
- 必须由单个线程调用，且在所有提交任务的线程返回之后再调用。

**普通接口**

- 该重载等待普通`channel`调用链产生的完成记录。

**批量接口**

- 调用前必须通过[batch_commit](./simt_batch_commit.md)提交当前批次中的全部任务。
- 使用批量句柄期间需要独占其关联的通道资源，不能混用普通`drain`。

## 调用示例

drain接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[04_simt_asccomm_urma](../../../../../examples/aicore/asccomm/04_simt_asccomm_urma/README.md)与[06_simt_asccomm_urma_batch_perf](../../../../../examples/aicore/asccomm/06_simt_asccomm_urma_batch_perf/README.md)。

普通接口：

```cpp
__simt_callee__ void simt_drain_op(ChannelHandle channel, __gm__ uint8_t* local_buffer,
    __gm__ uint8_t* remote_buffer)
{
    asc::simt::asccomm<> asccomm;
    asccomm.init(nullptr, 0);

    // 提交写任务
    asccomm.write_nbi(channel, remote_buffer, local_buffer, 1024U);

    // 阻塞等待普通通道上已提交任务完成
    asccomm.drain(channel);
}
```

批量接口：

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 批量工作区大小：不小于256字节，不随批量任务数增长
constexpr uint32_t BATCH_BUF_SIZE = 256U;

__simt_callee__ void simt_batch_drain_op(
    ChannelHandle channel, __gm__ uint8_t* local_buffer, __gm__ uint8_t* remote_base)
{
    // 声明批量UB工作区，起始地址按16字节对齐
    alignas(16) __ubuf__ uint8_t asccomm_local[BATCH_BUF_SIZE];

    asc::simt::asccomm<> asccomm;
    asccomm.init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区
    auto batch_handle = asccomm.make_batch_handle(channel, asccomm_local, BATCH_BUF_SIZE);

    // 提交第一批任务
    asccomm.write_nbi(batch_handle, remote_base, local_buffer, BYTES);
    asccomm.batch_commit(batch_handle);

    // 复用批量句柄提交下一批任务
    asccomm.write_nbi(batch_handle, remote_base + BYTES, local_buffer, BYTES);
    asccomm.batch_commit(batch_handle);

    // 等待累计提交任务生成的全部完成记录
    asccomm.drain(batch_handle);
}
```
