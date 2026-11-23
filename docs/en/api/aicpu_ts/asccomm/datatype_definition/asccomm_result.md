# asccomm_result

## Function Description

Header file path: `"aicpu_ts/asccomm/asccomm_result.h"`.

`asccomm_result` is the unified status code enumeration for AICPU_TS Asccomm data-plane APIs. It indicates the result of an API call.

## Type Definition

`asccomm_result` is declared as an unscoped enumeration in `include/aicpu_ts/asccomm/asccomm_result.h`:

```cpp
typedef enum {
    ASCCOMM_SUCCESS = 0,
    ASCCOMM_E_PARA = 1,
    ASCCOMM_E_PTR = 2,
    ASCCOMM_E_MEMORY = 3,
    ASCCOMM_E_INTERNAL = 4,
    ASCCOMM_E_NOT_SUPPORT = 5,
    ASCCOMM_E_NOT_FOUND = 6,
    ASCCOMM_E_UNAVAIL = 7,
    ASCCOMM_E_SYSCALL = 8,
    ASCCOMM_E_TIMEOUT = 9,
    ASCCOMM_E_OPEN_FILE_FAILURE = 10,
    ASCCOMM_E_TCP_CONNECT = 11,
    ASCCOMM_E_ROCE_CONNECT = 12,
    ASCCOMM_E_TCP_TRANSFER = 13,
    ASCCOMM_E_ROCE_TRANSFER = 14,
    ASCCOMM_E_RUNTIME = 15,
    ASCCOMM_E_DRV = 16,
    ASCCOMM_E_PROFILING = 17,
    ASCCOMM_E_CCE = 18,
    ASCCOMM_E_NETWORK = 19,
    ASCCOMM_E_AGAIN = 20,
    ASCCOMM_E_REMOTE = 21,
    ASCCOMM_E_SUSPENDING = 22,
    ASCCOMM_E_OPRETRY_FAIL = 23,
    ASCCOMM_E_OOM = 24,
    ASCCOMM_E_IN_STATUS = 1041,
    ASCCOMM_E_RESERVED
} asccomm_result;
```

## Enum Value Description

| Enum Value | Value | Description |
| --- | --- | --- |
| `ASCCOMM_SUCCESS` | 0 | Operation succeeded. |
| `ASCCOMM_E_PARA` | 1 | Invalid parameter. |
| `ASCCOMM_E_PTR` | 2 | Null pointer error. |
| `ASCCOMM_E_MEMORY` | 3 | Memory error. |
| `ASCCOMM_E_INTERNAL` | 4 | Internal error. |
| `ASCCOMM_E_NOT_SUPPORT` | 5 | Unsupported feature. |
| `ASCCOMM_E_NOT_FOUND` | 6 | Specified resource not found. |
| `ASCCOMM_E_UNAVAIL` | 7 | Resource unavailable. |
| `ASCCOMM_E_SYSCALL` | 8 | Failed to call a system interface. |
| `ASCCOMM_E_TIMEOUT` | 9 | Timeout. |
| `ASCCOMM_E_OPEN_FILE_FAILURE` | 10 | Failed to open a file. |
| `ASCCOMM_E_TCP_CONNECT` | 11 | Failed to establish a TCP connection. |
| `ASCCOMM_E_ROCE_CONNECT` | 12 | Failed to establish a RoCE connection. |
| `ASCCOMM_E_TCP_TRANSFER` | 13 | TCP transfer failed. |
| `ASCCOMM_E_ROCE_TRANSFER` | 14 | RoCE transfer failed. |
| `ASCCOMM_E_RUNTIME` | 15 | Failed to call a runtime API. |
| `ASCCOMM_E_DRV` | 16 | Failed to call a driver API. |
| `ASCCOMM_E_PROFILING` | 17 | Failed to call a profiling API. |
| `ASCCOMM_E_CCE` | 18 | Failed to call a CCE API. |
| `ASCCOMM_E_NETWORK` | 19 | Failed to call a network API. |
| `ASCCOMM_E_AGAIN` | 20 | Retry required. |
| `ASCCOMM_E_REMOTE` | 21 | Remote completion queue element error. |
| `ASCCOMM_E_SUSPENDING` | 22 | Communicator suspending error. |
| `ASCCOMM_E_OPRETRY_FAIL` | 23 | Retry constraint error. |
| `ASCCOMM_E_OOM` | 24 | Out of memory. |
| `ASCCOMM_E_IN_STATUS` | 1041 | Reserved error code that is not returned by current Asccomm data-plane APIs. |
| `ASCCOMM_E_RESERVED` | 1042 | Reserved error code that does not indicate a currently defined error type. |

## Constraints

- API functions return `int32_t`, and enumeration constants are compared as integer values. Callers can compare return values directly and do not need to convert them to `asccomm_result` first.
- The error codes actually returned by a specific API are determined by that API's return value description. Not all APIs return every error code in the preceding table.
- `ASCCOMM_E_RESERVED` is a reserved error code. For extended error codes without explicit values in this table, refer to the released header file.

## Example

```cpp
#include "aicpu_ts/asccomm/asccomm_primitives.h"
#include "aicpu_ts/asccomm/asccomm_result.h"

// thread is a valid ThreadHandle resource handle passed in from the control plane; channel is a valid ChannelHandle resource handle passed in from the control plane.
int32_t ret = asccomm_channel_fence_on_thread(thread, channel);
if (ret != ASCCOMM_SUCCESS) {
    return ret;
}
```
