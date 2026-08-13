# ReadNbi

## Function Description

Reads data from remote `src` to local `dst`. The interface provides an ordinary `ChannelHandle` overload and a BatchHandle overload. The ordinary overload submits a read task to the channel, while the batch overload only prepares a read WQE in the UB buffer of the BatchHandle for a later `BatchCommit`.

## Function Prototype

Ordinary interface:

```cpp
template <
    bool commit = true,
    pipe_t commitPipe = PIPE_S,
    pipe_t reqPipe = PIPE_MTE3,
    auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t ReadNbi(
    AscendC::ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

Batch interface:

```cpp
template <auto const& config = URMA_DEFAULT_CFG>
__aicore__ inline int32_t ReadNbi(
    BatchHandleType& batchHandle, GM_ADDR dst, GM_ADDR src, uint64_t len);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the ordinary interface. |
| `batchHandle` | Input/Output | Batch handle created by `MakeBatchHandle`. Its WQEBB and CQE counters are updated after a WQE is prepared successfully. |
| `dst` | Output | Absolute local destination GM address. |
| `src` | Input | Absolute remote source GM address. |
| `len` | Input | Read length in bytes. |

## Template Parameters

| Parameter | Description |
| --- | --- |
| `commit` | Whether the ordinary interface commits the task immediately. The batch interface does not provide this parameter. |
| `commitPipe` | Pipe used for commit by the ordinary interface. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for the request by the ordinary interface. Default: `PIPE_MTE3`. |
| `config` | URMA WQE control configuration. Default: `URMA_DEFAULT_CFG` (strongly ordered + fence + CQE enabled). The batch interface requires `inlineEn = 0`, and `cqe` must be `0` or `1`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The ordinary task is submitted or the batch WQE is prepared successfully. |
| `-1` | The operation fails. For the batch interface, this includes a length that cannot be represented in 32 bits or insufficient space in the UB buffer. |

## Constraints

### Ordinary Interface

- The communication channel must be initialized, and `Init` must provide temporary workspace before this interface is called.
- On the `COMM_PROTOCOL_UBC_CTP` path, `src` must fall within a remote buffer registered for the channel, and `dst` is a local destination address.

### Batch Interface

- Currently supported only on the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.
- Each batch read task occupies one 64-byte WQEBB. It is prepared only in UB and does not copy data to the GM SQ or ring the doorbell.
- `len` must not exceed `UINT32_MAX`.
- `config.inlineEn` must be `0`, and `config.cqe` can be `0` or `1`. Different read, write, and write-with-notify tasks in the same batch may use different `cqe` settings.
- `src` is not checked against the remote registration range again. The caller must ensure that the source range is covered by the remote token cached by `MakeBatchHandle` using `dstBase`.
- If length or buffer capacity validation fails, the interface returns `-1` and leaves the prepared counters in the BatchHandle unchanged.
- Call `BatchCommit` after preparation. The caller must exclusively own the channel while using the BatchHandle.

## Related Sample

[hcomm_write_read_nbi](../../../../../examples/hcomm_write_read_nbi/README_en.md) demonstrates the ordinary AIV direct-driven URMA read/write workflow. See the [Hcomm Usage Guide](../../../guide/hcomm_usage.md) for the basic BatchHandle workflow.
