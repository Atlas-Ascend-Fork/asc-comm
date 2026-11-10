# AinBarrierSession

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:11:05.469Z pushedAt=2026-11-09T06:47:54.950Z nl=2 -->

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

`AscendC::AinBarrierSession` is a barrier synchronization session within a communication team on the AI Core side. It binds the [Ain](./Ain.md) instance, the communication team, and the barrier resource index during construction, and performs barrier synchronization within the communication team when [Sync](./Sync.md) is called.

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
| `CommEngineMask` | Communication engine selection mask, defaulting to `AIN_MASK_DEFAULT`, which corresponds to the AIV (AI Vector, vector core) communication engine. |

**Table 2** API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `ain` | Input | `Ain` instance pointer. |
| `team` | Input | Communication team handle. |
| `index` | Input | Barrier resource index. Different indexes are used to isolate different barrier sessions. |

## Usage Constraints

- The host side shall create the team and prepare the synchronization memory used by barrier before call.
- `ain` must not be **nullptr**. `team` must be a valid device-side handle.
- `index` must correspond to an available barrier resource. Different concurrent barriers should use different `index` values for isolation.
- The `index` used when constructing `AinBarrierSession` must be consistent with the `contextIndex` of the input `ain` to ensure that the barrier session uses the corresponding communication context resources.
- Currently, only the `COMM_PROTOCOL_UB_CTP` protocol is supported.

## Call Example

The following shows how to call the AinBarrierSession API. For the complete sample, see [01_basic_ring](../../../../../examples/aicore/ain/01_basic_ring/README.md).

```cpp
// Initialize the UBuf (Unified Buffer) workspace size: no less than 512B.
constexpr uint32_t HCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_barrier_sync_kernel(GM_ADDR team)
{
    // Prepare the UBuf workspace; alignas(32) ensures the start address is 32-byte aligned.
    alignas(32) __ubuf__ uint8_t hcommLocal[HCOMM_BUF_SIZE];
    AscendC::AinDescriptorUbuf hcommUbuf{hcommLocal, HCOMM_BUF_SIZE, 0U};

    // Resource indexes of Ain and AinBarrierSession.
    uint32_t index = 0U;
    // Create an Ain object.
    AscendC::Ain ain(index);
    // Create an AinBarrierSession object.
    AscendC::AinBarrierSession barrierSession(&ain, team, index);
    // Perform barrier synchronization within the team.
    barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, hcommUbuf);
}
```
