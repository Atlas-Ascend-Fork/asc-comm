# HcommChannelInfoDump

## Function Description
Dumps, parses, and prints the diagnostics for a communication channel at a host safe point after `aclrtSynchronizeStream` reports an error. The API is safe to call unconditionally, with or without a recorded exception. It accepts an optional caller-allocated byte buffer (`char *`) that receives the raw collected snapshot for the caller to analyze.

The snapshot is written as raw bytes and does not expose any internal data structure. Query the required buffer size from [HcommChannelGetDumpInfoSize](./HcommChannelGetDumpInfoSize.md) and allocate accordingly.

## Function Prototype
```cpp
int32_t HcommChannelInfoDump(AscendC::ChannelHandle channel, char *buf = nullptr, uint32_t len = 0);
```

## Parameter Description
| Parameter | Input/Output | Description |
| --- | --- | --- |
| `channel` | Input | Communication channel handle owned by the operator. |
| `buf` | Output | Optional buffer that receives the raw byte snapshot. Set to `nullptr` when the caller only needs the printing side effect. When non-null, its length must be at least `HcommChannelGetDumpInfoSize()`. |
| `len` | Input | Size of `buf` in bytes. Ignored when `buf` is `nullptr`. |

## Return Value
| Return Value | Description |
| --- | --- |
| `0` | Dump or no-op succeeded. |
| `-1` | Dump failed. |

## Constraints
- Call this API at a host safe point, typically after stream synchronization fails.
- The API checks the pending-exception flag internally, so callers do not need to guard it with a manual error check. The dump is skipped (and success is returned) only when an exception callback is registered and no exception is pending.
- A successful dump clears the recorded exception state. Repeated calls after that return success without printing stale state. A failed dump keeps the state so the caller can retry.
- When `buf` is non-null and large enough, the API stores the raw bytes into it before running the printing side effect. When the dump is skipped (no pending exception), the buffer is zero-filled so the caller never reads stale bytes from a previous dump.
- The snapshot is returned as raw bytes and does not expose a public struct. Obtain the buffer size from `HcommChannelGetDumpInfoSize()`; do not hard-code it.

## Printed Output
The full output of one dump is first assembled into a single text block and written out once, so concurrent dumps from multiple ranks never interleave line-by-line. The output contains, in order:
- `Channel`: handle, valid flag, engine/protocol, buffer counts, and the `sqHead/sqTail/cqHead/cqTail` cursors.
- `SqContext`/`CqContext`: the queue context (`sqVa/depth/wqeSize`, etc.) and its `head/tail`. If a `head` or `tail` read fails, a separate `ERROR` line is emitted instead of appending a validity flag to the normal line.
- `WQE`: all `HcommUrmaSqeCtx` fields printed in field order, followed by the trailing `SGE`; for `opcode == WRITE_WITH_NOTIFY` the `notify` context is printed as well.
- `CQE`: printed in `HcommUrmaJfcCqeCtx` field order.

The sampled range covers outstanding work: WQEs over `[sqTail, sqHead)` and CQEs over `[cqTail, cqHead)` (cursors are free-running counters, so the subtraction is wrap-safe).

> **Note (root-cause confirmation):** If `sqHead/sqTail/cqHead/cqTail` are all `0`, it is typically because the kernel exited abnormally (e.g. via `trap()`) without running `dcci` to flush the `ChannelEntity` cursors out of cache, so the host reads back the initial zeros from DRAM. This is a kernel-side cache-coherence limitation that the DFX collection layer cannot work around. In that case the outstanding range is empty and the dump prints no misleading WQE/CQE, which is expected behavior.
