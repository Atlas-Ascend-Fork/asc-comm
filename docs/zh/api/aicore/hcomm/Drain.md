# Drain

## 功能说明

阻塞等待指定普通通道或BatchHandle已提交的通信任务执行完成。

普通重载依赖`Init`提供的CQE临时工作区；批量重载不依赖`Init`，而是复用BatchHandle中WQE缓冲区的第一个64字节WQEBB作为CQE临时空间。

## 函数原型

普通接口：

```cpp
template <pipe_t pipe = PIPE_MTE3>
__aicore__ inline int32_t Drain(AscendC::ChannelHandle channel);
```

批量接口：

```cpp
template <
    pipe_t pipe = PIPE_MTE3,
    typename T,
    typename BatchHandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t Drain(T& batchHandle);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道或多通道批量句柄，用于等待该句柄已提交的通信任务完成。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `pipe` | drain使用的pipe，默认`PIPE_MTE3`。 |
| `T` | 批量句柄类型，由`batchHandle`实参推导；当前支持`UbcCtpBatchHandle`和`UbcCtpMultiBatchHandle`。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 等待成功。 |
| `-1` | 参数或BatchHandle状态无效，或对应协议返回通用失败。 |
| 其他非`0`值 | `COMM_PROTOCOL_UBC_CTP`路径直接返回底层CQ轮询错误码：`0xFF`表示轮询超时，其他正值由CQE的status和substatus组合而成。 |

## 约束说明

### 普通接口

- 调用前需要通过`Init`提供临时工作区。
- 该重载等待普通`ChannelHandle`调用链记录的CQE完成。

### 批量接口

- 当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径，不需要调用`Init`。
- 多通道模式下，将外层多通道批量句柄传入批量`Drain`。
- 调用前必须先通过`BatchCommit`提交已准备的WQE。BatchHandle中不能存在尚未提交的WQEBB，否则返回`-1`。
- 批量`Drain`复用WQE缓冲区的第一个64字节作为CQE临时空间。调用期间不得并发访问或改写该缓冲区。
- 可以执行多次`BatchCommit`后统一调用一次批量`Drain`，接口会等待BatchHandle累计记录的全部CQE。
- 当已提交任务全部配置为`cqe = 0`时，没有待轮询CQE，接口会直接返回成功，但该返回值不能用于确认硬件已经完成这些请求。在顺序和fence配置能够保证最后一个请求在前序请求之后完成时，可以仅将最后一条请求配置为`cqe = 1`，然后通过批量`Drain`确认整组任务完成。
- BatchHandle使用期间需要独占对应单通道或共享Jetty，不能混用普通`Drain`。

## 相关接口

- [GetHandleRef](./GetHandleRef.md)
- [BatchCommit](./BatchCommit.md)
