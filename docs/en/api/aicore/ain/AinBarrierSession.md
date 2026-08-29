# AinBarrierSession

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

Header file:

```cpp
#include "ain/ain.h"
```

`AscendC::AinBarrierSession` is an AICore-side in-team synchronization session implemented based on AIN signals. The constructor binds an `Ain` instance, a communication team and a barrier resource index. When `Sync` is called, it sends signals to other ranks in the team and waits until the corresponding signals from other ranks reach the threshold.

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

## Parameter Description

**Table 1**  Template parameters

| Parameter | Description |
| --- | --- |
| `CommEngineMask` | Communication engine selection mask. Default value: `AIN_MASK_DEFAULT`. This parameter is currently reserved. |

**Table 2**  API parameters

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `ain` | Input | Pointer to an `Ain` instance. |
| `team` | Input | Communication team handle. |
| `index` | Input | Barrier resource index. Different indexes isolate different barrier sessions. |

## Common APIs

| API | Description |
| --- | --- |
| [Sync](./Sync.md) | Perform in-team barrier synchronization. Both no-timeout and timeout overloads are supported. |

## Constraints

- The Host side shall create the team and prepare the synchronization memory used by barrier before invocation.
- `ain` must not be nullptr. `team` must be a valid device-side handle.
- `index` must correspond to an available barrier resource. Different concurrent barriers should use different `index` values for isolation.
- `Sync` updates remote signals through underlying Hcomm atomic add operations, and polls local signals to wait until other ranks arrive.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.

## Example

The calling example of AinBarrierSession is as follows:

```cpp
AscendC::Ain<> ain;
AscendC::AinBarrierSession<> barrierSession(&ain, team, 0);
barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX);    // Perform in-team barrier synchronization
```
