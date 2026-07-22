# Hcomm
## Function Description
Header file:
```cpp
#include "hcomm/hcomm.h"
```
`AscendC::Hcomm` is a point-to-point communication interface template on the AICore side. It supports submitting read, write, write-with-notify and atomic operation tasks via communication channels. Task submission and completion waiting are controlled through `Commit` and `Drain`. This document describes protocol capabilities and usage constraints for RoCE and UBC_CTP/URMA paths.

## Template Parameters
```cpp
template <AscendC::CommProtocol commProtocol = AscendC::COMM_PROTOCOL_UBC_CTP>
class Hcomm;
```

| Parameter | Description |
| --- | --- |
| `commProtocol` | Communication protocol type. Supports `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP`. Default value: `COMM_PROTOCOL_UBC_CTP`. |

## Protocol Capabilities
| API | `COMM_PROTOCOL_ROCE` | `COMM_PROTOCOL_UBC_CTP` |
| --- | --- | --- |
| `Init` | Supported; requires UB temporary workspace. | Supported; requires URMA temporary workspace. |
| `ReadNbi` | Supported. | Supported. |
| `WriteNbi` | Supported. | Supported. |
| `WriteWithNotifyNbi` | Not supported; invocation returns failure. | Supported. |
| `AtomicFAA` | Not supported. | Supported. |
| `AtomicCAS` | Not supported. | Supported. |
| `Commit` | Supported. | Supported. |
| `Drain` | Supported. | Supported. |

## Common APIs
| API | Description |
| --- | --- |
| [Init](./Init_en.md) | Initialize Hcomm temporary workspace. |
| [ReadNbi](./ReadNbi_en.md) | Submit a read task via the specified channel. |
| [WriteNbi](./WriteNbi_en.md) | Submit a write task via the specified channel. |
| [WriteWithNotifyNbi](./WriteWithNotifyNbi_en.md) | Submit a write task and write a notification value. |
| [AtomicFAA](./AtomicFAA_en.md) | Submit a Fetch-and-add atomic operation task. |
| [AtomicCAS](./AtomicCAS_en.md) | Submit a Compare-and-swap atomic operation task. |
| [Commit](./Commit_en.md) | Explicitly submit pending tasks on the channel. |
| [Drain](./Drain_en.md) | Wait for all communication tasks on the channel to complete. |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Execution succeeded. |
| `-1` | Execution failed. |

## Usage Constraints
- The communication channel shall be initialized by the caller before invoking communication APIs.
- Both `COMM_PROTOCOL_ROCE` and `COMM_PROTOCOL_UBC_CTP` paths require a temporary workspace provided via `Init`. The layout of the temporary workspace differs between protocols.
- `WriteWithNotifyNbi` is only available for the `COMM_PROTOCOL_UBC_CTP` path; calls on the `COMM_PROTOCOL_ROCE` path will return failure.
- `AtomicFAA` and `AtomicCAS` are only supported for the `COMM_PROTOCOL_UBC_CTP` path. Supported data types are limited to `int32_t`, `uint32_t`, `int64_t`, `uint64_t`.
- The passed `ChannelHandle` must point to a channel entity matching the selected protocol.
