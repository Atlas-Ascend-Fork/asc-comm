# AinBarrierSession

## Function Description

Header file:

```cpp
#include "ain/ain.h"
```

`AscendC::AinBarrierSession` is an AICore-side in-team synchronization session implemented based on AIN signals. The constructor binds an `Ain` instance, a communication team and a barrier resource index. When `Sync` is called, it sends signals to other ranks in the team and waits until the corresponding signals from other ranks reach the threshold.

## Template Parameters

```cpp
template <unsigned CommEngineMask = AscendC::AIN_MASK_DEFAULT>
class AinBarrierSession;
```

| Parameter | Description |
| --- | --- |
| `CommEngineMask` | Communication engine selection mask. Default value: `AIN_MASK_DEFAULT`. This parameter is currently reserved. |

## Constructor

```cpp
__aicore__ inline AinBarrierSession(
    AscendC::Ain<CommEngineMask>* ain,
    AscendC::HcommTeamHandle team,
    uint32_t index);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `ain` | Input | Pointer to an `Ain` instance. |
| `team` | Input | Communication team handle. |
| `index` | Input | Barrier resource index. Different indexes isolate different barrier sessions. |

## Common APIs

| API | Description |
| --- | --- |
| [Sync](./Sync.md) | Perform in-team barrier synchronization. Both no-timeout and timeout overloads are supported. |

## Usage Constraints

- The Host side shall create the team and prepare the synchronization memory used by barrier before invocation.
- `ain` must not be nullptr. `team` must be a valid device-side handle.
- `index` must correspond to an available barrier resource. Different concurrent barriers should use different `index` values for isolation.
- `Sync` updates remote signals through underlying Hcomm atomic add operations, and polls local signals to wait until other ranks arrive.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
