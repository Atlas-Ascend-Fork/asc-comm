# Init

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas 推理系列产品AI Core：不支持
- Atlas 推理系列产品Vector Core：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas 训练系列产品：不支持
<!-- end id6 -->

## 功能说明

初始化普通`ChannelHandle`接口使用的Hcomm临时工作区。`COMM_PROTOCOL_ROCE`和`COMM_PROTOCOL_UBC_CTP`路径的普通接口均需要在提交通信任务前调用该接口。

BatchHandle调用链不依赖`Init`，其工作区通过`MakeBatchHandle`提供。

## 函数原型

```cpp
__aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

template <typename T>
__aicore__ inline int32_t Init(const AscendC::LocalTensor<T>& buff, uint32_t len);
```

## 参数说明

**表1**  模板参数说明

| 参数 | 说明 |
| --- | --- |
| `T` | `LocalTensor`的元素类型，仅用于`LocalTensor`重载。 |

**表2**  接口参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `buff` | 输入 | 调用方提供的UB缓冲区或`LocalTensor`。 |
| `len` | 输入 | 缓冲区长度，单位为字节。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 初始化成功。 |
| `-1` | 初始化失败。 |

## 约束说明

- RoCE路径会使用`buff`作为WQE/CQE/doorbell临时工作区，当前最小工作区大小为512字节。
- UBC_CTP/URMA路径会使用`buff`作为WQE/CQE临时工作区，当前最小工作区大小为512字节。
- 使用`__ubuf__ uint8_t*`初始化时，实现会对`buff`按32字节对齐后的地址作为临时工作区起始地址。
- 使用`LocalTensor`初始化时，实现直接使用传入tensor。RoCE路径要求`len`和`buff.GetSize()`均不小于512字节；UBC_CTP/URMA路径要求`len`不小于512字节且不能超过`buff.GetSize()`。
- 该接口只用于普通`ChannelHandle`调用链，BatchHandle接口不需要调用该接口。

## 调用示例

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::LocalTensor<uint8_t> tmpBuffer; // 已从UB内存中分配，容量不小于512字节
uint32_t tmpLen = 512U;

int32_t ret = hcomm.Init(tmpBuffer, tmpLen);
```
