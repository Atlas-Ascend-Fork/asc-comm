# Ain

## Product Support

<!-- npu="950" id1 -->
- Ascend 950PR/Ascend 950DT: Supports the URMA protocol
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
<!-- npu="x90" id7 -->
- Kirin X90: Not supported
<!-- end id7 -->

## Function Description

The header file path is `"comm_api/aicore/ain/ain.h"`.

`AscendC::Ain` is an AICore-side one-sided communication interface template. It provides one-sided communication and synchronization capabilities such as `Put`/`Get`/`Signal`/`Flush`. `Ain` resolves communication channels and GM addresses from the communication team, peer rank and symmetric window handles, so the Kernel side does not need to maintain remote addresses directly.

## Function Prototype

```cpp
template <unsigned CommEngineMask = AscendC::AIN_MASK_DEFAULT>
class Ain;
```

Constructor:

```cpp
__aicore__ inline Ain(uint32_t contextIndex = 0);
```

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `CommEngineMask` | Communication engine selection mask. Default value: `AIN_MASK_DEFAULT`. This parameter is currently reserved. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `contextIndex` | Input | Communication context index. Default value: 0. |

## Common APIs

| API | Description |
| --- | --- |
| [Put](./Put.md) | Write data from a local symmetric window to a peer symmetric window. |
| [PutValue](./PutValue.md) | Write an immediate value to a peer symmetric window. |
| [Get](./Get.md) | Read data from a peer symmetric window to a local symmetric window. |
| [Flush](./Flush.md) | Wait for communication tasks on all peer channels in the team to complete. |
| [FlushAsync](./FlushAsync.md) | Get the communication channel handle of a specified peer for subsequent asynchronous waiting. |
| [Wait](./Wait.md) | Wait for tasks on a specified communication channel to complete. |
| [Signal](./Signal.md) | Perform an atomic add operation on a remote signal. |
| [ReadSignal](./ReadSignal.md) | Read a local signal value. |
| [WaitSignal](./WaitSignal.md) | Wait until a local signal reaches the specified threshold. |

## Constraints

- The Host side shall create the team and register symmetric windows before invocation.
- The passed `HcommTeamHandle` and `HcclCommSymWindow` must be valid device-side handles.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
- `Put`, `PutValue`, `Get` and `Signal` initialize the underlying Hcomm internally. A valid `AinDescriptorUbuf` must be provided when invoking these APIs.
- When `AIN_COMMIT_DELAYED` is used, the number of consecutive calls must not exceed the underlying SQ depth (`sqDepth`). Accumulated tasks must be submitted via `AIN_COMMIT_IMMED` before the SQ is exhausted; otherwise, subsequent tasks will fail due to SQ overflow.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls plus a final `AIN_COMMIT_IMMED`), only the last submission should produce a CQE.

## Example

The calling example of Ain is as follows:

```cpp
// Size of data transferred in a single communication
constexpr uint64_t BYTES = 1024;

extern "C" __global__ __aicore__ void ain_kernel(
      __gm__ void* team, __gm__ void* dstWin, __gm__ void* srcWin,
      uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        AscendC::HCOMM_URMA_TMP_BUF_SIZE, 0U};
    // Create an Ain object
    AscendC::Ain ain;
    // Peer rank ID
    uint32_t peer = (rankId + 1) % rankNum;
    // One-sided write: write data from the local srcWin to the peer dstWin
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    // Wait for communication tasks on all peer channels in the team to complete
    ain.Flush(team);
}
```
