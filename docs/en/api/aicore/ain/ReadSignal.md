# ReadSignal

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T07:01:43.217Z pushedAt=2026-10-26T07:32:09.946Z nl=2 -->

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

Reads the local signal value of the local rank, and returns the value masked to the low-order width specified by `bits`.

`ReadSignal` resolves the signal address corresponding to the current rank based on `team`, `signalWindow` and `signalOffset`, and reads the `uint64_t` signal value through a device-side read operation.

## Function Prototype

```cpp
__aicore__ inline uint64_t ReadSignal(
    AscendC::HcommTeamHandle team,
    AscendC::HcclCommSymWindow signalWindow,
    size_t signalOffset,
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
| `bits` | Input | Number of low-order bits to read, ranging from 1 to 64, defaulting to **64**. |
| `order` | Input | Signal read memory order, defaulting to `AIN_MEMORY_ORDER_RELAX`. |

## Return Value

| Return Value | Description |
| --- | --- |
| `uint64_t` | Value obtained by masking the low `bits` of the local signal of this rank. |

## Usage Constraints

- The host side shall create the team and register the symmetric window containing the signal before the call.
- `team` and `signalWindow` must be valid device-side handles.
- The address corresponding to `signalOffset` must fall within the registered memory range of the signal window corresponding to the local rank.
- The signal address shall be prepared according to `uint64_t` access requirements.
- `bits` is used to generate the low-order mask. Valid range: 1-64. When `bits` is 64, the complete 64-bit signal value is read.
- Currently, only `AIN_MEMORY_ORDER_RELAX` is supported. This memory order only guarantees signal atomic read/write and threshold checking, and does not guarantee the memory order for ordinary data accesses before and after signal operations.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The following is an example of calling the **ReadSignal** API.

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
    // Peer rankId
    uint32_t peer = (rankId + 1) % rankNum;
    // Perform an atomic increment on the remote signal, adding 1 per call.
    ain.Signal<AscendC::AinSignalInc>(team, peer,
        AscendC::AinSignalInc{signalWin, 0U}, hcommUbuf);
    // Expected signal threshold
    uint64_t expectVal = 1U;
    // Block and wait for the signal.
    while(true) {
        // Read the local signal value.
        uint64_t signalVal = ain.ReadSignal(team, signalWin, 0U);
        // Block until the signal from the remote end is received: signalVal >= expectVal
        if (signalVal >= expectVal) {
            break;
        }
    }
}
```
