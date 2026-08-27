# HcommChannelInfoDump

## Function Description
Dumps and prints the diagnostics for a communication channel at a host safe point after `aclrtSynchronizeStream` reports an error. The API is safe to call unconditionally and returns success without printing when no exception has been recorded.

## Function Prototype
```cpp
int32_t HcommChannelInfoDump(AscendC::ChannelHandle channel);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle owned by the operator. |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Dump or no-op succeeded. |
| `-1` | Dump failed. |

## Constraints
- Call this API at the host safe point after stream synchronization fails.
- The API checks the pending-exception flag internally, so callers do not need to guard it with a manual error check.
- The first successful dump consumes the recorded exception state. Repeated calls after that return success without printing stale state.
