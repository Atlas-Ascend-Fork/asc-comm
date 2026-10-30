# Init

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

头文件路径：`"hcomm/hcomm.h"`。

初始化普通`ChannelHandle`接口使用的Hcomm临时工作区。`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UB_CTP`协议的普通接口均需要在提交通信任务前调用该接口。

BatchHandle调用链不依赖`Init`，其工作区通过[MakeBatchHandle](./MakeBatchHandle.md)提供。

## 函数原型

```cpp
__aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

template <typename T>
__aicore__ inline int32_t Init(const AscendC::LocalTensor<T>& buff, uint32_t len);
```

## 参数说明

**表1** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `buff` | 输入 | 调用方提供的UB工作区，可通过裸指针或`LocalTensor`传入。 |
| `len` | 输入 | 缓冲区长度，单位为字节。 |

## 返回值说明

**表2** 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 初始化成功。 |
| `-1` | 初始化失败。 |

## 约束说明

- 支持`COMM_PROTOCOL_UB_CTP`与`COMM_PROTOCOL_ROCE`协议。
- `COMM_PROTOCOL_ROCE`与`COMM_PROTOCOL_UB_CTP`协议均使用`buff`作为通信任务组装所需的临时工作区，当前最小工作区大小为512字节，起始地址必须按32字节对齐。
- 使用`__ubuf__ uint8_t*`初始化时，需传入32字节对齐的起始地址；使用`LocalTensor`初始化时，工作区使用传入tensor的起始地址，tensor容量按字节计为`buff.GetSize() * sizeof(T)`。`COMM_PROTOCOL_ROCE`协议要求`len`和`buff.GetSize() * sizeof(T)`均不小于512字节；`COMM_PROTOCOL_UB_CTP`协议要求`len`不小于512字节且不超过`buff.GetSize() * sizeof(T)`。
- 该接口只用于普通`ChannelHandle`调用链，BatchHandle批量接口不需要调用该接口。

## 调用示例

Init接口的调用示例如下。为聚焦调用流程，示例省略返回值检查与错误处理，完整样例参见[01_hcomm_urma_write_read_nbi](../../../../../examples/aicore/hcomm/01_hcomm_urma_write_read_nbi/README.md)。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UB临时工作区大小：不得小于512字节
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void hcomm_init_kernel(
    uint64_t channel, GM_ADDR localBuffer, GM_ADDR remoteBuffer)
{
    // 准备UB临时工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];

    // 创建Hcomm对象并初始化UB临时工作区
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    hcomm.Init(hcommLocal, HCOMM_BUF_SIZE);

    // 初始化完成后即可调用普通接口提交通信任务
    hcomm.WriteNbi(channel, remoteBuffer, localBuffer, BYTES);

    // 阻塞等待通道上已提交任务完成
    hcomm.Drain(channel);
}
```
