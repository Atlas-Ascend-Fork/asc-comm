# AtomicCAS

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

提交Compare-and-swap原子操作任务，比较远端地址`dst`中的值和`compareVal`，相等时将远端值替换为`swapVal`，并将替换前的旧值写入本端`fetchAddr`。接口提供普通`channel`重载、批量`batchHandle`重载和批量Group协作重载：批量重载将原子操作任务添加到当前批次，后续由[BatchCommit](./SIMT_BatchCommit.md)统一提交。

## 函数原型

普通接口：

```cpp
template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal);
```

批量接口：

```cpp
template <typename T, auto const& config = URMA_DEFAULT_CFG>
__simt_callee__ inline int32_t AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal);
```

批量Group协作接口：

```cpp
template <typename T, auto const& config = URMA_DEFAULT_CFG, typename Group>
__simt_callee__ inline int32_t AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal,
    const Group& group);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `T` | 原子操作数据类型，仅支持`int32_t`、`uint32_t`、`int64_t`、`uint64_t`。 |
| `commit` | 普通接口是否在提交任务时立即commit。`false`表示延迟提交，后续由`commit`为`true`的调用统一触发。 |
| `config` | URMA任务配置，默认为`URMA_DEFAULT_CFG`。 |
| `Group` | SIMT协作组类型，由批量Group协作重载使用。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `channel` | 输入 | 普通接口使用的通信通道句柄。 |
| `batchHandle` | 输入/输出 | [MakeBatchHandle](./SIMT_MakeBatchHandle.md)创建的批量句柄。 |
| `dst` | 输入/输出 | 远端原子操作目标GM地址。 |
| `fetchAddr` | 输出 | 本端GM地址，用于保存远端地址执行CAS前的旧值。 |
| `compareVal` | 输入 | 比较值。 |
| `swapVal` | 输入 | 比较成功时写入远端目标地址的新值。 |
| `group` | 输入 | SIMT协作组，批量Group协作重载使用。 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `0` | 普通任务提交成功，或原子操作任务成功添加到当前批次。 |
| 非`0` | 操作失败。 |

## 约束说明

**普通接口**

- 调用前通信通道资源需已完成初始化。
- `dst`需要落在通道注册的远端buffer范围内，`fetchAddr`用于保存旧值，长度为`sizeof(T)`。

**批量接口**

- 添加完成后需要调用[BatchCommit](./SIMT_BatchCommit.md)提交当前批次。

## 调用示例

AtomicCAS接口的调用示例如下。

普通接口：

```cpp
// 比较值
constexpr uint64_t COMPARE_VAL = 0U;
// 比较成功时写入远端目标地址的新值
constexpr uint64_t SWAP_VAL = 1U;

__simt_callee__ void SimtAtomicCasOp(
    AscendC::ChannelHandle channel, __gm__ uint8_t* remoteValue, __gm__ uint8_t* fetchBuffer)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 远端remoteValue与COMPARE_VAL相等时替换为SWAP_VAL，替换前的旧值写入本端fetchBuffer
    hcomm.AtomicCAS<uint64_t>(channel, remoteValue, fetchBuffer, COMPARE_VAL, SWAP_VAL);

    // 阻塞等待原子操作任务完成
    hcomm.Drain(channel);
}
```

批量接口：

```cpp
// 比较值
constexpr uint64_t COMPARE_VAL = 0U;
// 比较成功时写入远端目标地址的新值
constexpr uint64_t SWAP_VAL = 1U;
// 批量工作区大小
constexpr uint32_t BATCH_BUF_SIZE = 128U;

__simt_callee__ void SimtBatchAtomicCasOp(
    AscendC::ChannelHandle channel, __ubuf__ uint8_t* batchBuffer, __gm__ uint8_t* remoteValue,
    __gm__ uint8_t* fetchBuffer, __gm__ uint8_t* remoteBase)
{
    AscendC::simt::Hcomm<> hcomm;
    hcomm.Init(nullptr, 0);

    // 创建批量句柄并绑定批量工作区，remoteBase用于选择远端注册内存
    auto batchHandle = hcomm.MakeBatchHandle(channel, batchBuffer, BATCH_BUF_SIZE, remoteBase);

    // 向当前批次添加原子操作任务
    hcomm.AtomicCAS<uint64_t>(batchHandle, remoteValue, fetchBuffer, COMPARE_VAL, SWAP_VAL);

    // 提交当前批次中的全部任务
    hcomm.BatchCommit(batchHandle);

    // 等待已提交任务生成的完成记录
    hcomm.Drain(batchHandle);
}
```
