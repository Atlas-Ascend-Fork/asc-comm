# AinBarrierSession

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T06:56:11.743Z pushedAt=2026-10-26T07:32:09.930Z nl=2 -->

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

`AscendC::AinBarrierSession` is an AI Core-side in-team synchronization session. The constructor binds an `Ain` instance, a communication team, and a barrier resource index; calling `Sync` implements barrier synchronization within the communication team.

## Function Prototype

```cpp
template <unsigned CommEngineMask = AscendC::AIN_MASK_DEFAULT>
class AinBarrierSession;
```

Constructor:

```cpp
__aicore__ inline AinBarrierSession(
    AscendC::Ain<CommEngineMask>* ain,
    AscendC::HcommTeamHandle team,
    uint32_t index);
```

## Parameters

**Table 1** Template parameters

| Parameter | Description |
| --- | --- |
| `CommEngineMask` | Communication engine selection mask. The default value is `AIN_MASK_DEFAULT`, which corresponds to the AIV communication engine. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `ain` | Input | `Ain` instance pointer. |
| `team` | Input | Handle of the communication team. |
| `index` | Input | Barrier resource index. Different indexes are used to isolate different barrier sessions. |

## Usage Constraints

- The host side shall create the team and prepare the synchronization memory used by barrier before the call.
- `ain` must not be **nullptr**. `team` must be a valid device-side handle.
- `index` must correspond to an available barrier resource. Different concurrent barriers should use different `index` values for isolation.
- The `index` used when constructing `AinBarrierSession` must be consistent with the `contextIndex` of the input `ain` to ensure that the barrier session uses the corresponding communication context resources.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol path is supported.

## Example

The following is an example of calling the **AinBarrierSession** API.

```cpp
// Initialize the UBuf workspace size: it must be no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __global__ __aicore__ void ain_kernel(__gm__ void* team)
{
    // Prepare the UBuf workspace.
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf;
    pipe.InitBuffer(hcommBuf, HCOMM_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf.Get<uint8_t>();
    AscendC::AinDescriptorUbuf hcommUbuf{
        reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr()),
        HCOMM_BUF_SIZE, 0U};

    // Resource indexes of Ain and AinBarrierSession.
    uint32_t index = 0U;
    // Create an Ain object
    AscendC::Ain ain(index);
    // Create an AinBarrierSession object.
    AscendC::AinBarrierSession barrierSession(&ain, team, index);
    // Perform barrier synchronization within the team.
    barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, hcommUbuf);
}
```
