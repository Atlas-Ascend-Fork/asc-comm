# init

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

初始化普通`ChannelHandle`接口使用的asccomm临时工作区。`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UB_CTP`协议的普通接口均需要在提交通信任务前调用该接口。

batch_handle调用链不依赖`init`，其工作区通过[make_batch_handle](./make_batch_handle.md)提供。

## 函数原型

```cpp
__aicore__ inline int32_t init(__ubuf__ uint8_t* buff, uint32_t len);

```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `buff` | 输入 | 调用方提供的UB工作区起始地址（`__ubuf__ uint8_t*`）。 |
| `len` | 输入 | 缓冲区长度，单位为字节。 |

## 返回值说明

**表2** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 初始化成功。 |

接口约束不满足时（如`buff`未按32字节对齐、`len`小于512字节）通过调试期断言（Trap）报告错误，不返回错误码。

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- `COMM_PROTOCOL_ROCE`与`COMM_PROTOCOL_UB_CTP`协议均使用`buff`作为通信任务组装所需的临时工作区，当前最小工作区大小为512字节，起始地址必须按32字节对齐。
- `buff`需传入32字节对齐的UB起始地址。`COMM_PROTOCOL_ROCE`协议要求`len`不小于512字节；`COMM_PROTOCOL_UB_CTP`协议要求`len`不小于512字节。
- 该接口只用于普通`ChannelHandle`调用链，batch_handle批量接口不需要调用该接口。

## 调用示例

init接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[01_asccomm_urma_write_read_nbi](../../../../../examples/aicore/asccomm/01_asccomm_urma_write_read_nbi/README.md)。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t ASCCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void init_kernel(
    uint64_t channel, GM_ADDR local_buffer, GM_ADDR remote_buffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[ASCCOMM_BUF_SIZE];

    // 创建asccomm对象并初始化UB临时工作区
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    asccomm.init(asccomm_local, ASCCOMM_BUF_SIZE);

    // 初始化完成后即可调用普通接口提交通信任务
    asccomm.write_nbi(channel, remote_buffer, local_buffer, BYTES);

    // 阻塞等待通道上已提交任务完成
    asccomm.drain(channel);
}
```
