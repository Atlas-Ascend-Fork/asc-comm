# Drain

## 功能说明

阻塞等待指定普通通道的通信任务完成，或等待BatchHandle已提交任务生成的完成记录。

普通重载依赖`Init`提供的临时工作区；批量重载使用`MakeBatchHandle`绑定的工作区，不依赖`Init`。

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
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t Drain(T& batchHandle);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | `MakeBatchHandle`创建的单通道或多通道批量句柄，用于等待该句柄已提交任务生成的完成记录。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `pipe` | drain使用的pipe，默认`PIPE_MTE3`。 |
| `T` | 批量句柄类型，由`batchHandle`实参推导。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 等待成功。 |
| `-1` | 参数或BatchHandle状态无效，或对应协议返回通用失败。 |
| `0xFF` | `COMM_PROTOCOL_UBC_CTP`路径下，完成队列轮询超时。 |
| 除`0xFF`外的其他正值 | `COMM_PROTOCOL_UBC_CTP`路径下，完成记录报告错误；返回值高8位为`status`，低8位为`substatus`。 |

## 约束说明

### 普通接口

- 调用前需要通过`Init`提供临时工作区。
- 该重载等待普通`ChannelHandle`调用链产生的完成记录。

### 批量接口

- 当前仅支持Ascend 950上的`COMM_PROTOCOL_UBC_CTP`路径，不需要调用`Init`。
- 多通道模式下，将`MakeBatchHandle`返回的多通道批量句柄传入批量`Drain`。
- 调用前必须通过`BatchCommit`提交当前批次中的全部任务。存在尚未提交的任务时调用将返回`-1`。
- 可以执行多次`BatchCommit`后统一调用一次批量`Drain`，接口会等待该BatchHandle累计提交任务生成的全部完成记录。
- 若所有已提交任务均配置为`cqe = 0`，本接口会直接返回，但不能据此确认这些任务已经完成。如需通过本接口确认整批任务完成，需要为能够代表整批完成的任务配置`cqe = 1`。默认配置`URMA_DEFAULT_CFG`的`cqe`为`1`，因此每个任务都会生成完成记录。
- BatchHandle使用期间需要独占其关联的通道资源，不能混用普通`Drain`。

## 相关接口

- [GetHandleRef](./GetHandleRef.md)
- [BatchCommit](./BatchCommit.md)
