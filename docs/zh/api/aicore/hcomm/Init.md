# Init

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

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `buff` | 输入 | 调用方提供的UB工作区，可通过裸指针或`LocalTensor`传入。 |
| `len` | 输入 | 缓冲区长度，单位为字节。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 初始化成功。 |
| `-1` | 初始化失败。 |

## 约束说明

- RoCE路径会使用`buff`作为通信任务和完成状态管理所需的临时工作区，当前最小工作区大小为512字节。
- UBC_CTP/URMA路径会使用`buff`作为通信任务和完成状态管理所需的临时工作区，当前最小工作区大小为512字节。
- 使用`__ubuf__ uint8_t*`初始化时，实现会对`buff`按32字节对齐后的地址作为临时工作区起始地址。
- 使用`LocalTensor`初始化时，实现直接使用传入tensor。RoCE路径要求`len`和`buff.GetSize()`均不小于512字节；UBC_CTP/URMA路径要求`len`不小于512字节且不能超过`buff.GetSize()`。
- 该接口只用于普通`ChannelHandle`调用链，BatchHandle接口不需要调用该接口。
