# Ain

## Function Description

Header file:

```cpp
#include "ain/ain.h"
```

`AscendC::Ain` is an AICore-side one-sided communication interface template. It provides one-sided communication and synchronization capabilities such as `Put`/`Get`/`Signal`/`Flush`. `Ain` resolves communication channels and GM addresses from the communication team, peer rank and symmetric window handles, so the Kernel side does not need to maintain remote addresses directly.

## Template Parameters

```cpp
template <unsigned CommEngineMask = AscendC::AIN_MASK_DEFAULT>
class Ain;
```

| Parameter | Description |
| --- | --- |
| `CommEngineMask` | Communication engine selection mask. Default value: `AIN_MASK_DEFAULT`. This parameter is currently reserved. |

## Constructor

```cpp
__aicore__ inline Ain(uint32_t contextIndex = 0);
```

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

## Usage Constraints

- The Host side shall create the team and register symmetric windows before invocation.
- The passed `HcommTeamHandle` and `HcommWindowHandle` must be valid device-side handles.
- Currently, only the `COMM_PROTOCOL_UBC_CTP` protocol path is supported.
- `Put`, `PutValue`, `Get` and `Signal` initialize the underlying Hcomm internally. A valid `AinDescriptorUbuf` must be provided when invoking these APIs.
