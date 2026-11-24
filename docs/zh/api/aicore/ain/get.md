# get

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

头文件路径：`"ain/ain.h"`。

通过指定通信组team和对端rank ID，提交单边读任务，将对端对称window中的数据读取到本端对称window。

`get`会根据`team`、`peer`和`ain`实例的`context_index`解析通信通道，根据`dst_win`、`src_win`及offset解析本端/远端GM（Global Memory，全局内存）地址，并通过底层asccomm读接口提交通信任务。

## 函数原型

```cpp
template <
    typename descriptor_ubuf = ain_descriptor_ubuf,
    ain_commit_flags commit_flags = AIN_COMMIT_IMMED,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline void get(
    HcommTeamHandle team,
    uint32_t peer,
    HcclCommSymWindow dst_win,
    uint64_t dst_offset,
    HcclCommSymWindow src_win,
    uint64_t src_offset,
    uint64_t bytes,
    const descriptor_ubuf& ubuf = descriptor_ubuf{});
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `descriptor_ubuf` | UBuf（Unified Buffer，统一缓冲区）临时工作区描述类型，默认`ain_descriptor_ubuf`。 |
| `commit_flags` | 任务提交方式。`AIN_COMMIT_IMMED`表示组装WQE（Work Queue Element，工作队列元素）后立即触发提交；`AIN_COMMIT_DELAYED`表示只组装WQE，需后续调用一个`AIN_COMMIT_IMMED`任务统一触发提交。 |
| `config` | `COMM_PROTOCOL_UB_CTP` WQE控制配置，默认`URMA_DEFAULT_CFG`（执行序为relax order + CQE保序上报 + 启用fence + 需要上报CQE）。自定义`config`可调整`cqe`等字段。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `team` | 输入 | 通信组team句柄。 |
| `peer` | 输入 | team内的对端rank ID。 |
| `dst_win` | 输入 | 本端目的对称window句柄。 |
| `dst_offset` | 输入 | 目的对称window内的字节偏移。 |
| `src_win` | 输入 | 对端源对称window句柄。 |
| `src_offset` | 输入 | 源对称window内的字节偏移。 |
| `bytes` | 输入 | 读取长度，单位为字节。 |
| `ubuf` | 输入 | 底层asccomm使用的UBuf（Unified Buffer，统一缓冲区）临时工作区描述。 |

其中，`ubuf`的类型`ain_descriptor_ubuf`包含三个字段：`addr`（`__ubuf__ uint8_t*`，UBuf工作区起始地址）、`bytes`（`uint32_t`，工作区大小，单位为字节）和`eventId`（`uint32_t`，预留用于与底层通信引擎同步的事件ID，当前固定填`0`）。

## 返回值说明

无返回值。

## 约束说明

- 调用前Host侧需完成team创建及对称window注册。
- `team`、`dst_win`和`src_win`需要是有效的device侧句柄；`peer`需要是team内有效rank ID。
- `dst_offset + bytes`需要落在本rank对应的`dst_win`注册内存范围内，`src_offset + bytes`需要落在`peer`对应的`src_win`注册内存范围内。
- `ubuf.addr`和`ubuf.bytes`需要提供可供底层asccomm初始化使用的UBuf临时工作区；`descriptor_ubuf`当前必须提供不小于512B且起始地址按32字节对齐的UBuf临时工作区。
- 使用`AIN_COMMIT_DELAYED`时，本次任务不会立即触发提交，需要后续至少提交一次`AIN_COMMIT_IMMED`的任务来保证之前任务已被提交。
- 使用`AIN_COMMIT_DELAYED`时，连续积攒的任务槽位不得超过通道的任务提交容量（即底层SQ（Send Queue，发送队列）深度sqDepth）；需在容量耗尽前通过`AIN_COMMIT_IMMED`提交积攒的任务，否则后续任务将因SQ溢出而失败。`put`/`put_value`/`get`各占1个任务槽位；`signal`占2个任务槽位。
- 批量提交场景（多次`AIN_COMMIT_DELAYED` + 最后一次`AIN_COMMIT_IMMED`）下，仅最后一次提交应产生CQE（Completion Queue Entry，完成队列项），需将中间任务的`config`中`cqe`字段设为0、最后一次任务设为1。
- `get`提交的是非阻塞通信任务；需要调用[flush](./flush.md)等待team内所有peer通道任务完成，或通过[flush_async](./flush_async.md)获取指定peer通道后调用[wait](./wait.md)等待完成。
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议。单次调用`get`的入参`bytes`取值范围为`0 < bytes ≤ 256 * 1024 * 1024`。

## 调用示例

Get接口的调用示例如下，完整样例参见[01_basic_ring](../../../../../examples/aicore/ain/01_basic_ring/README.md)。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UBuf（Unified Buffer，统一缓冲区）工作区大小：不得小于512B
constexpr uint32_t ASCCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_get_kernel(
    GM_ADDR team, GM_ADDR dst_win, GM_ADDR src_win,
    uint32_t rank_num, uint32_t rank_id)
{
    // 准备UBuf工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[ASCCOMM_BUF_SIZE];
    ain_descriptor_ubuf ubuf{asccomm_local, ASCCOMM_BUF_SIZE, 0U};
    // 创建Ain对象
    asc::ain ain;
    // 对端rank_id
    uint32_t peer = (rank_id + 1) % rank_num;
    // 单边读：对端srcWin数据读取到本端dstWin
    ain.get(team, peer, dst_win, rank_id * BYTES, src_win, 0U, BYTES, ubuf);
    // 等待team内所有peer通道通信任务完成
    ain.flush(team);
}
```
