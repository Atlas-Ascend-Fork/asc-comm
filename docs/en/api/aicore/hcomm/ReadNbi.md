# ReadNbi

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 training series products/Atlas A3 inference series products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 training series products/Atlas A2 inference series products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference series products AI Core: Not supported
- Atlas inference series products Vector Core: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training series products: Not supported
<!-- end id6 -->

## Function Description

Reads data from remote `src` to local `dst`. The interface provides an ordinary `ChannelHandle` overload and a BatchHandle overload. The ordinary overload submits a read task to the channel, while the batch overload adds a read task to the current batch for a later `BatchCommit`.

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
template <
    auto const& config = URMA_DEFAULT_CFG,
    typename T,
    typename HandleTraits<T>::ChannelType* = nullptr>
__aicore__ inline int32_t ReadNbi(
    T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len);
```

## Parameter Description

**Table 1**  Template Parameter Description

| Parameter | Description |
| --- | --- |
| `commit` | Whether the ordinary interface commits the task immediately. The batch interface does not provide this parameter. |
| `commitPipe` | Pipe used for commit by the ordinary interface. Default: `PIPE_S`. |
| `reqPipe` | Pipe used for the request by the ordinary interface. Default: `PIPE_MTE3`. |
| `config` | URMA task configuration. Default: `URMA_DEFAULT_CFG`. The batch interface requires `inlineEn = 0`, and `cqe` must be `0` or `1`. |
| `T` | Batch handle type, deduced from `batchHandle`. |

**Table 2**  Interface Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle used by the ordinary interface. |
| `batchHandle` | Input/Output | Single-channel batch handle created by `MakeBatchHandle`, or a BatchHandle reference returned by `GetHandleRef`. |
| `dst` | Output | Absolute local destination GM address. |
| `src` | Input | Absolute remote source GM address. |
| `len` | Input | Read length in bytes. |

## Return Value

| Return Value | Description |
| --- | --- |
| `0` | The ordinary task is submitted, or the read task is added to the current batch successfully. |
| `-1` | The operation fails. For the batch interface, this includes insufficient space in the UB buffer. |

## Constraints

### Ordinary Interface

- The communication channel must be initialized, and `Init` must provide temporary workspace before this interface is called.
- On the `COMM_PROTOCOL_UBC_CTP` path, `src` must fall within a remote buffer registered for the channel, and `dst` is a local destination address.

### Batch Interface

- Currently supported only on the `COMM_PROTOCOL_UBC_CTP` path on Ascend 950.
- Each batch read task uses 64 bytes of batch workspace.
- For a batch handle, `[src, src + len)` must belong to its selected remote registered memory.
- The interface returns `-1` if the remaining batch workspace is insufficient.
- Call `BatchCommit` after adding tasks. In multi-channel mode, add tasks through the handle reference returned by `GetHandleRef` and submit them through the multi-channel batch handle returned by `MakeBatchHandle`. Exclusively own the associated channel resources while using the BatchHandle.

## Example

Ordinary interface:

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::ChannelHandle channel; // Initialized UBC_CTP channel handle
GM_ADDR localDst; // Valid local destination address
GM_ADDR remoteSrc; // Registered remote source address
uint64_t dataLen = 64U;

int32_t ret = hcomm.ReadNbi(channel, localDst, remoteSrc, dataLen);
if (ret == 0) {
    ret = hcomm.Drain(channel);
}
```

Batch interface:

```cpp
AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
AscendC::ChannelHandle channel; // Initialized UBC_CTP channel handle
AscendC::LocalTensor<uint8_t> batchBuffer; // Allocated from UB memory
GM_ADDR remoteSrc; // Registered remote source address
GM_ADDR localDst; // Valid local destination address
uint32_t batchBufferLen = 64U;
uint32_t dataLen = 64U;

auto batchHandle = hcomm.MakeBatchHandle(
    channel, batchBuffer, batchBufferLen, remoteSrc);
int32_t ret = hcomm.ReadNbi(batchHandle, localDst, remoteSrc, dataLen);
if (ret == 0) {
    ret = hcomm.BatchCommit(batchHandle);
}
if (ret == 0) {
    ret = hcomm.Drain(batchHandle);
}
```
