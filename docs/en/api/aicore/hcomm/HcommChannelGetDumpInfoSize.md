# HcommChannelGetDumpInfoSize

## Function Description
Returns the number of bytes a caller must allocate to receive one raw channel dump snapshot. Allocate a buffer of at least this size and pass it (as `char *`) to [HcommChannelInfoDump](./HcommChannelInfoDump.md).

## Function Prototype
```cpp
uint32_t HcommChannelGetDumpInfoSize();
```

## Parameter Description
None.

## Return Value
| Return Value | Description |
| --- | --- |
| `>0` | Size in bytes of one raw dump snapshot. |

## Constraints
- The buffer passed to `HcommChannelInfoDump` must be at least this many bytes.
- The value is stable within a build; recompute it after upgrading the library and do not hard-code it.
- The snapshot is returned as raw bytes; this API reports only the size and does not expose the internal data-structure layout.
