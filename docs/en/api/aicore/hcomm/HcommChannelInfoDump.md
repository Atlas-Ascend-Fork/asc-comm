# HcommChannelInfoDump

## Function Description
Dumps, parses, and prints the diagnostics for a communication channel at a host safe point after `aclrtSynchronizeStream` reports an error. The API is safe to call unconditionally and can also return the parsed dump data to the caller.

## Function Prototype
```cpp
int32_t HcommChannelInfoDump(AscendC::ChannelHandle channel, AscendC::HcommDfxChannelDumpData *dump = nullptr);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle owned by the operator. |
| `dump` | Output | Parsed dump data. Set to `nullptr` when the caller only needs the default printing. |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Dump or no-op succeeded. |
| `-1` | Dump failed. |

## Constraints
- Call this API at the host safe point after stream synchronization fails.
- The API checks the pending-exception flag internally, so callers do not need to guard it with a manual error check.
- The first successful dump consumes the recorded exception state. Repeated calls after that return success without printing stale state.
- When `dump` is not `nullptr`, the API fills the parsed structure before printing.
