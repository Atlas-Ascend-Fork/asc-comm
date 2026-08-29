# WriteValueNbi

## 功能说明

提交点对点立即数写任务：将`value`按值携带在WQE中，写入远端`dst`。与`WriteNbi`从本端`src`缓冲区拷贝数据不同，`WriteValueNbi`的源数据由实参`value`直接提供并内联在WQE（Inline Write），不依赖本端源缓冲区，适用于写入单个标量/立即数的场景。

`WriteValueNbi`仅在`COMM_PROTOCOL_UBC_CTP`（通过URMA实现）路径下支持。接口将`value`封装为`UdmaParams<T>`并提交`HcommUrmaOpCode::WRITE`任务，`src`为空、长度固定为`sizeof(T)`。

## 函数原型

```cpp
template <
    typename T,
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_INLINE_CFG>
__aicore__ inline int32_t WriteValueNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, T value);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 通信通道句柄。 |
| `dst` | 输出 | 远端目的GM绝对地址。 |
| `value` | 输入 | 需要写入远端的立即数值，按`sizeof(T)`字节内联携带在WQE中。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 立即数`value`的类型，`sizeof(T)`即写入宽度。 |
| `commit` | 是否在提交任务时立即commit。`true`表示组装WQE后立即提交；`false`表示只组装WQE，需后续调用`Commit`提交。默认`true`。 |
| `commitPipe` | commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA WQE控制配置，默认`URMA_INLINE_CFG`（强保序 + fence + 使能CQE + **Inline**模式，即`inlineEn = 1`）。立即数写要求`inlineEn`必须为`1`，否则编译报错。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 任务提交成功。 |
| `-1` | 操作失败。`COMM_PROTOCOL_ROCE`路径不支持该接口，调用返回`-1`。 |

## 约束说明

- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- 传入的`ChannelHandle`需要对应`COMM_PROTOCOL_UBC_CTP`通道。
- `config.inlineEn`必须为`1`（即使用内联模式），否则编译报错。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。

## 调用示例

以默认`commit=true`立即提交方式向远端`dst`写入一个`uint32_t`立即数：

```cpp
// 使用默认模板参数：立即提交，commitPipe=PIPE_S, reqPipe=PIPE_MTE3, config=URMA_INLINE_CFG
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;

// 分配UB临时工作区并用 Init 初始化（普通接口提交任务前必须先调用 Init）
AscendC::TBuf<AscendC::TPosition::VECOUT> workspaceBuf;
pipe->InitBuffer(workspaceBuf, HCOMM_WORKSPACE_SIZE); // HCOMM_WORKSPACE_SIZE 不小于 512
AscendC::LocalTensor<uint8_t> workspace = workspaceBuf.Get<uint8_t>();

// channel 需已完成通道初始化
hcomm.WriteValueNbi<uint32_t>(
    channel,                                  // 通信通道句柄
    reinterpret_cast<GM_ADDR>(remoteDstAddr), // 远端目的GM地址
    0x12345678U);                   // 内联写入远端的立即数
// 阻塞等待已提交任务完成
hcomm.Drain(channel);
```

延迟提交方式（`commit=false`），先组装WQE，后续统一`Commit`提交：

```cpp
// 分配UB临时工作区并用 Init 初始化，channel 需已完成通道初始化
AscendC::LocalTensor<uint8_t> workspace = workspaceBuf.Get<uint8_t>();
hcomm.Init(workspace, HCOMM_WORKSPACE_SIZE);

// commit=false：只组装WQE，不立即提交
hcomm.WriteValueNbi<uint32_t, false>(
    channel,
    reinterpret_cast<GM_ADDR>(remoteDstAddr),
    0x12345678U);
// 提交任务
hcomm.Commit(channel);
// 阻塞等待已提交任务完成
hcomm.Drain(channel);
```
