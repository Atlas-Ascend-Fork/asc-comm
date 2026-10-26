# Ain

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T06:55:32.524Z pushedAt=2026-10-26T07:32:09.930Z nl=2 -->

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

`AscendC::Ain` is an AI Core-side one-sided communication API template. It provides one-sided communication and synchronization capabilities such as `Put`/`Get`/`Signal`/`Flush`. `Ain` resolves the communication channel and communication memory address based on the communication team, peer rank, and symmetric window handle, so the kernel side does not need to directly maintain the remote address.

## Function Prototype

```cpp
template <unsigned CommEngineMask = AscendC::AIN_MASK_DEFAULT>
class Ain;
```

Constructor:

```cpp
__aicore__ inline Ain(uint32_t contextIndex = 0);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `CommEngineMask` | Communication engine selection mask. Default value: `AIN_MASK_DEFAULT`, which corresponds to the AIV communication engine. Currently only AIV is supported; other communication engines are not supported. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `contextIndex` | Input | Communication context index, defaulting to **0**. |

## Usage Constraints

- The host side shall create the team and register symmetric windows before the call.
- The passed `HcommTeamHandle` and `HcclCommSymWindow` must be valid device-side handles.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.
- `Put`, `PutValue`, `Get` and `Signal` initialize the underlying Hcomm internally. A valid `AinDescriptorUbuf` must be provided when calling these APIs.
- When using `AIN_COMMIT_DELAYED`, the number of consecutive calls must not exceed the underlying SQ depth (`sqDepth`). The accumulated tasks must be submitted through `AIN_COMMIT_IMMED` before the SQ is exhausted; otherwise, subsequent tasks will fail due to SQ overflow.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls + a final `AIN_COMMIT_IMMED` call), only the last submission should generate a CQE.

## Example

The following is an example of calling the **Ain** API.

```cpp
// Data size transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf workspace size: it must be no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(
    __gm__ void* team, __gm__ void* dstWin, __gm__ void* srcWin,
    uint32_t rankNum, uint32_t rankId)
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
    // One-sided write: write the local srcWin data to the peer dstWin.
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    // Wait for the communication tasks of all peer channels in the team to complete
    ain.Flush(team);
}
```
