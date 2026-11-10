# Ain

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:11:07.915Z pushedAt=2026-11-09T06:47:54.952Z nl=2 -->

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

Header file path: `"ain/ain.h"`.

`AscendC::Ain` is a one-sided communication API template on the AI Core side that provides one-sided communication and synchronization capabilities such as `Put`/`Get`/`Signal`/`Flush`. `Ain` resolves the communication channel and communication memory address based on the communication group team, the peer rank, and the symmetric window handle, so the kernel side does not need to maintain remote addresses directly. Here, the team is a communication group created on the host side, the rank is the node number within the communicator, and the symmetric window is a symmetric memory window registered by all ranks in the communication at the same address and size. One-sided tasks access the peer memory based on the window handle and offset.

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
| `CommEngineMask` | Communication engine selection mask. Default value: `AIN_MASK_DEFAULT`, which corresponds to the AIV (AI Vector) communication engine. Currently only AIV is supported; other communication engines are not supported. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `contextIndex` | Input | Communication context index, defaulting to **0**. |

## Usage Constraints

- The host side shall create the team and register symmetric windows before call.
- The passed `HcommTeamHandle` and `HcclCommSymWindow` must be valid device-side handles.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported.
- [Put](./Put.md), [PutValue](./PutValue.md), [Get](./Get.md), and [Signal](./Signal.md) internally initialize the underlying Hcomm communication component (point-to-point communication engine; see [Hcomm](../hcomm/Hcomm.md)). A valid `AinDescriptorUbuf` must be provided when calling them.
- When `AIN_COMMIT_DELAYED` is used, the continuously accumulated task slots must not exceed the task submission capacity of the channel (that is, the depth sqDepth of the underlying SQ (Send Queue)). The accumulated tasks must be submitted through `AIN_COMMIT_IMMED` before the capacity is exhausted; otherwise, subsequent tasks will fail due to SQ overflow. `Put`/`PutValue`/`Get` each occupy 1 task slot, and `Put`/`PutValue` additionally append 2 task slots (3 in total) when the `AinSignalInc`/`AinSignalAdd` remote action is configured. `Signal` occupies 2 task slots.
- In the batch submission scenario (multiple `AIN_COMMIT_DELAYED` calls followed by a final `AIN_COMMIT_IMMED`), only the final submission should generate a CQE (Completion Queue Entry). Set the `cqe` field in the `Config` of intermediate tasks to 0 and that of the final task to 1. By default, the `cqe` of `URMA_DEFAULT_CFG`/`URMA_INLINE_CFG` (URMA, Unified Remote Memory Access) is 1, so each task generates a CQE if the configuration is not adjusted.

## Call Example

The following shows an example of calling the Ain API. For the complete sample, see [01_basic_ring](../../../../../examples/aicore/ain/01_basic_ring/README.md).

```cpp
// Size of data transferred in a single communication.
constexpr uint64_t BYTES = 1024U;
// Initialize the UBuf (Unified Buffer) workspace size: no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_put_kernel(
    GM_ADDR team, GM_ADDR dstWin, GM_ADDR srcWin,
    uint32_t rankNum, uint32_t rankId)
{
    // Prepare the UBuf workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];
    AscendC::AinDescriptorUbuf hcommUbuf{hcommLocal, HCOMM_BUF_SIZE, 0U};
    // Create the Ain object.
    AscendC::Ain ain;
    // Peer rankId.
    uint32_t peer = (rankId + 1) % rankNum;
    // One-sided write: write data from the local srcWin to the peer dstWin.
    ain.Put(team, peer, dstWin, rankId * BYTES, srcWin, 0U, BYTES,
            AscendC::AinRemoteNone{}, hcommUbuf);
    // Wait for all peer channel communication tasks in the team to complete.
    ain.Flush(team);
}
```
