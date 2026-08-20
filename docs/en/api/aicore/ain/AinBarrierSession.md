# ain_barrier_session

## Function Description

Header file:

```cpp
#include "ain/ain.h"
```

`AscendC::ain_barrier_session` is an AICore-side in-team synchronization session implemented based on AIN signals. The constructor binds an `ain` instance, a communication team and a barrier resource index. When `sync` is called, it sends signals to other ranks in the team and waits until the corresponding signals from other ranks reach the threshold.

## Template Parameters

```cpp
template <unsigned comm_engine_mask = AscendC::ain_mask_default>
class ain_barrier_session;
```

| Parameter | Description |
| --- | --- |
| `comm_engine_mask` | Communication engine selection mask. Default value: `ain_mask_default`. This parameter is currently reserved. |

## Constructor

```cpp
__aicore__ inline ain_barrier_session(
    AscendC::ain<comm_engine_mask>* ain,
    AscendC::asccomm_team_handle team,
    uint32_t index);
```

## Parameter Description

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `ain` | Input | Pointer to an `ain` instance. |
| `team` | Input | Communication team handle. |
| `index` | Input | Barrier resource index. Different indexes isolate different barrier sessions. |

## Common APIs

| API | Description |
| --- | --- |
| [sync](./Sync.md) | Perform in-team barrier synchronization. Both no-timeout and timeout overloads are supported. |

## Usage Constraints

- The Host side shall create the team and prepare the synchronization memory used by barrier before invocation.
- `ain` must not be nullptr. `team` must be a valid device-side handle.
- `index` must correspond to an available barrier resource. Different concurrent barriers should use different `index` values for isolation.
- `sync` updates remote signals through underlying asccomm atomic add operations, and polls local signals to wait until other ranks arrive.
- Currently, only the `comm_protocol_ubc_ctp` protocol path is supported.
