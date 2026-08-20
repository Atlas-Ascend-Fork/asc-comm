# ain

## Function Description

Header file:

```cpp
#include "ain/ain.h"
```

`AscendC::ain` is an AICore-side one-sided communication interface template. It provides one-sided communication and synchronization capabilities such as `put`/`get`/`signal`/`flush`. `ain` resolves communication channels and GM addresses from the communication team, peer rank and symmetric window handles, so the Kernel side does not need to maintain remote addresses directly.

## Template Parameters

```cpp
template <unsigned comm_engine_mask = AscendC::ain_mask_default>
class ain;
```

| Parameter | Description |
| --- | --- |
| `comm_engine_mask` | Communication engine selection mask. Default value: `ain_mask_default`. This parameter is currently reserved. |

## Constructor

```cpp
__aicore__ inline ain(uint32_t context_index = 0);
```

| Parameter | Input/Output | Description |
| --- | --- | --- |
| `context_index` | Input | Communication context index. Default value: 0. |

## Common APIs

| API | Description |
| --- | --- |
| [put](./Put.md) | Write data from a local symmetric window to a peer symmetric window. |
| [put_value](./PutValue.md) | Write an immediate value to a peer symmetric window. |
| [get](./Get.md) | Read data from a peer symmetric window to a local symmetric window. |
| [flush](./Flush.md) | Wait for communication tasks on all peer channels in the team to complete. |
| [flush_async](./FlushAsync.md) | Get the communication channel handle of a specified peer for subsequent asynchronous waiting. |
| [wait](./Wait.md) | Wait for tasks on a specified communication channel to complete. |
| [signal](./Signal.md) | Perform an atomic add operation on a remote signal. |
| [read_signal](./ReadSignal.md) | Read a local signal value. |
| [wait_signal](./WaitSignal.md) | Wait until a local signal reaches the specified threshold. |

## Usage Constraints

- The Host side shall create the team and register symmetric windows before invocation.
- The passed `asccomm_team_handle` and `asccomm_window_handle` must be valid device-side handles.
- Currently, only the `comm_protocol_ubc_ctp` protocol path is supported.
- `put`, `put_value`, `get` and `signal` initialize the underlying asccomm internally. A valid `ain_descriptor_ubuf` must be provided when invoking these APIs.
