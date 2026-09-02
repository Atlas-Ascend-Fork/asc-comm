# HcommChannelInfoDump

## Function Description
Dumps, parses, and prints the diagnostics for a communication channel at a host safe point after `aclrtSynchronizeStream` reports an error. The API is safe to call unconditionally, with or without a recorded exception. It accepts an optional output buffer that receives the raw collected data.

## Function Prototype
```cpp
int32_t HcommChannelInfoDump(AscendC::ChannelHandle channel, AscendC::HcommDfxChannelDumpData *dump = nullptr);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle owned by the operator. |
| `dump` | Output | Optional raw collected data. Set to `nullptr` when the caller only needs the printing side effect. |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Dump or no-op succeeded. |
| `-1` | Dump failed. |

## Constraints
- Call this API at a host safe point, typically after stream synchronization fails.
- The API checks the pending-exception flag internally, so callers do not need to guard it with a manual error check. The dump is skipped (and success is returned) only when an exception callback is registered and no exception is pending.
- A successful dump clears the recorded exception state. Repeated calls after that return success without printing stale state. A failed dump keeps the state so the caller can retry.
- When `dump` is not `nullptr`, the API stores the raw collected data into it before running the printing side effect.
