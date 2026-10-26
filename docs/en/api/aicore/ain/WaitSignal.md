# WaitSignal

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T07:05:36.456Z pushedAt=2026-10-26T07:32:09.949Z nl=2 -->

## Supported Products

<!-- npu="950" id1 -->
- Ascend 950PR&950DT products: Supported
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Not supported
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2 products: Not supported
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2 inference products: Not supported
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas inference products: Not supported
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas training products: Not supported
<!-- end id6 -->

## Function Description

Blocks and waits until the local signal value of the local rank reaches the specified threshold.

`WaitSignal` resolves the signal address corresponding to the local rank based on `team`, `signalWindow`, and `signalOffset`, and repeatedly reads the signal value masked to the low `bits` until the value is greater than or equal to `least`.

## Function Prototype

```cpp
__aicore__ inline void WaitSignal(
    AscendC::HcommTeamHandle team,
    AscendC::HcclCommSymWindow signalWindow,
    size_t signalOffset,
    uint64_t least,
    uint32_t bits = 64,
    AscendC::AinMemoryOrder order = AscendC::AIN_MEMORY_ORDER_RELAX) const;
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `team` | Input | Handle of the communication team. |
| `signalWindow` | Input | Handle of the symmetric window where the signal resides. |
| `signalOffset` | Input | Byte offset of the signal within the symmetric window. |
| `least` | Input | Threshold to wait for. |
| `bits` | Input | Number of low-order bits to compare, ranging from 1 to 64, defaulting to **64**. |
| `order` | Input | Memory order for signal waiting, defaulting to `AIN_MEMORY_ORDER_RELAX`. |

## Return Value

No return value.

## Usage Constraints

- The host side shall create the team and register the symmetric window containing the signal before the call.
- `team` and `signalWindow` must be valid device-side handles.
- The address corresponding to `signalOffset` must fall within the registered memory range of the signal window corresponding to the local rank.
- The signal address shall be prepared according to `uint64_t` access requirements.
- `bits` is used to generate the low-order mask. Valid range: 1-64. When `bits` is 64, the complete 64-bit signal value is compared.
- Currently, only `AIN_MEMORY_ORDER_RELAX` is supported. This memory order only guarantees signal atomic read/write and threshold checking, and does not guarantee the memory order for ordinary data accesses before and after signal operations.
- `WaitSignal` keeps polling until the condition is satisfied. The caller must ensure that the remote side will update this signal to avoid permanent kernel waiting.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The following is an example of calling the **WaitSignal** API.

```cpp
// Initialize the UBuf workspace size: it must be no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* signalWin, uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace.
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        HCOMM_BUF_SIZE, 0U};
    // Create an Ain object.
    AscendC::Ain ain;
    // Peer rankId.
    uint32_t peer = (rankId + 1) % rankNum;
    // Expected signal threshold.
    uint64_t expectValue = 1U;
    // Send a signal to the peer, and atomically increment the peer signalValue by 1.
    ain.Signal<AscendC::AinSignalInc>(team, peer,
        AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);
    // Wait for the signal task submission to complete.
    ain.Flush(team);
    // Block until the local signal value reaches the threshold: local signalValue >= expectValue.
    ain.WaitSignal(team, signalWin, 0U, expectValue);
}
```
