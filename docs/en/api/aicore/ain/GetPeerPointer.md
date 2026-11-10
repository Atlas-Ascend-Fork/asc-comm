# GetPeerPointer

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:13:08.534Z pushedAt=2026-11-09T06:47:54.952Z nl=2 -->

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

This API is a helper API for [Ain](./Ain.md) communication, used to obtain the memory address at a specified offset in the symmetric window of the peer rank ID.

`GetPeerPointer` obtains the remote symmetric memory address based on the peer rank ID `peer` and the byte offset `offset`, for directly accessing the peer window memory.

## Function Prototype

```cpp
__aicore__ inline AscendC::HcommMemHandle GetPeerPointer(uint32_t peer, AscendC::HcclCommSymWindow window, size_t offset);
```

## Parameters

**Table 1** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `peer` | Input | Peer rank ID, that is, the member ID in the team. |
| `window` | Input | Symmetric window handle of the `HcclCommSymWindow` type, registered on the host and then delivered to the device. |
| `offset` | Input | Byte offset within the window, of the `size_t` type. |

## Return Value

Returns a memory address of the `HcommMemHandle` type, pointing to the memory at the `offset` in the symmetric window of the peer rank ID.

## Usage Constraints

- Before the call, the host must complete symmetric window registration, and the `window` passed in must be a valid device-side handle.
- `peer` must be a valid rank ID in the team, and `offset` must not exceed the window size; otherwise, the returned address is invalid, and accessing memory through this address is undefined behavior.
- After the symmetric window is deregistered, the returned memory address is no longer valid and must not be used to access memory.

## Call Example

The following is an example of calling the **GetPeerPointer** API.

```cpp
extern "C" __vector__ __global__ __aicore__ void ain_get_peer_pointer_kernel(
    GM_ADDR srcWin,
    uint32_t rankNum, uint32_t rankId)
{
    // Peer rankId.
    uint32_t peer = (rankId + 1) % rankNum;
    // Obtain the memory address at offset 0 in the symmetric window of the peer.
    AscendC::HcommMemHandle peerPtr = AscendC::GetPeerPointer(peer, srcWin, 0U);
    // peerPtr is the GM (Global Memory) address of the peer window memory and can be accessed directly.
    *reinterpret_cast<__gm__ uint64_t*>(peerPtr) = rankId;
}
```
