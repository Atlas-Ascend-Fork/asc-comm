# WriteValueNbi

## 功能说明

提交点对点立即数写任务：将`value`按值携带在WQE中，写入远端`dst`。与`WriteNbi`从本端`src`缓冲区拷贝数据不同，`WriteValueNbi`的源数据由实参`value`直接提供并内联在WQE（Inline Write），不依赖本端源缓冲区，适用于写入单个标量/立即数的场景。

当前该接口仅支持`COMM_PROTOCOL_UBC_CTP`路径。

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
| `dst` | 输出 | 远端目的地址。 |
| `value` | 输入 | 需要写入远端的立即数值，按`sizeof(T)`字节内联携带在WQE中。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `T` | 立即数`value`的类型，支持`int8_t`、`uint8_t`、`int16_t`、`uint16_t`、`half`、`bfloat16_t`、`int32_t`、`uint32_t`、`float`、`int64_t`、`uint64_t`、`double`。`sizeof(T)`即写入宽度。 |
| `commit` | 是否在提交任务时立即commit。`true`表示组装WQE后立即提交；`false`表示只组装WQE，需后续调用`Commit`提交。默认`true`。 |
| `commitPipe` | commit使用的pipe，默认`PIPE_S`。 |
| `reqPipe` | 请求使用的pipe，默认`PIPE_MTE3`。 |
| `config` | URMA WQE控制配置，默认`URMA_INLINE_CFG`（执行序为relax order + CQE保序上报 + 使能fence + 需要上报CQE + 携带inline数据）。立即数写要求`inlineEn`必须为`1`，否则编译报错。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 提交成功。 |
| `-1` | 提交失败。 |

## 约束说明

- 调用前通信通道需已完成初始化，并通过`Init`提供临时工作区。
- 传入的`ChannelHandle`需要对应`COMM_PROTOCOL_UBC_CTP`通道。
- `config.inlineEn`必须为`1`（即使用内联模式），否则编译报错。
- 若`commit`模板参数设为`false`，连续调用次数不得超过`sqDepth`，需在SQ耗尽前通过`Commit`提交积攒的任务，否则后续`PostSend`将因SQ溢出而失败。
- 批量提交场景（多次延迟commit + 最后一次commit）下，仅最后一次commit应产生CQE（即中间任务的`config.cqe`设为0，最后一次设为1）。
