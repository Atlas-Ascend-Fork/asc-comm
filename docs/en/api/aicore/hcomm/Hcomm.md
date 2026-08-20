# asccomm

## Function Description
Header file:
```cpp
#include "hcomm/hcomm.h"
```
`AscendC::asccomm` is a point-to-point communication interface template on the AICore side. It supports submitting read, write, write-with-notify and atomic operation tasks via communication channels. Task submission and completion waiting are controlled through `commit` and `drain`. This document describes protocol capabilities and usage constraints for RoCE and UBC_CTP/URMA paths.

## Template Parameters
```cpp
template <AscendC::comm_protocol comm_protocol = AscendC::comm_protocol_ubc_ctp>
class asccomm;
```

| Parameter | Description |
| --- | --- |
| `comm_protocol` | Communication protocol type. Supports `comm_protocol_roce` and `comm_protocol_ubc_ctp`. Default value: `comm_protocol_ubc_ctp`. |

## Protocol Capabilities
| API | `comm_protocol_roce` | `comm_protocol_ubc_ctp` |
| --- | --- | --- |
| `init` | Supported; requires UB temporary workspace. | Supported; requires URMA temporary workspace. |
| `read_nbi` | Supported. | Supported. |
| `write_nbi` | Supported. | Supported. |
| `write_with_notify_nbi` | Not supported; invocation returns failure. | Supported. |
| `atomic_faa` | Not supported. | Supported. |
| `atomic_cas` | Not supported. | Supported. |
| `commit` | Supported. | Supported. |
| `drain` | Supported. | Supported. |

## Common APIs
| API | Description |
| --- | --- |
| [init](./Init.md) | Initialize asccomm temporary workspace. |
| [read_nbi](./ReadNbi.md) | Submit a read task via the specified channel. |
| [write_nbi](./WriteNbi.md) | Submit a write task via the specified channel. |
| [write_with_notify_nbi](./WriteWithNotifyNbi.md) | Submit a write task and write a notification value. |
| [atomic_faa](./AtomicFAA.md) | Submit a Fetch-and-add atomic operation task. |
| [atomic_cas](./AtomicCAS.md) | Submit a Compare-and-swap atomic operation task. |
| [commit](./Commit.md) | Explicitly submit pending tasks on the channel. |
| [drain](./Drain.md) | Wait for all communication tasks on the channel to complete. |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Execution succeeded. |
| `-1` | Execution failed. |

## Usage Constraints
- The communication channel shall be initialized by the caller before invoking communication APIs.
- Both `comm_protocol_roce` and `comm_protocol_ubc_ctp` paths require a temporary workspace provided via `init`. The layout of the temporary workspace differs between protocols.
- `write_with_notify_nbi` is only available for the `comm_protocol_ubc_ctp` path; calls on the `comm_protocol_roce` path will return failure.
- `atomic_faa` and `atomic_cas` are only supported for the `comm_protocol_ubc_ctp` path. Supported data types are limited to `int32_t`, `uint32_t`, `int64_t`, `uint64_t`.
- The passed `channel_handle` must point to a channel entity matching the selected protocol.

## Related Sample
[hcomm_write_read_nbi](../../../../../examples/hcomm_write_read_nbi/README_en.md) demonstrates that the AIV Kernel invokes `write_nbi` and `read_nbi` over the `comm_protocol_ubc_ctp` path in a two-card scenario. This sample does not cover the RoCE path.
