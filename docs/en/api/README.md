# API Reference

## AICore Hcomm
| Document | Description |
| --- | --- |
| [Hcomm](./aicore/hcomm/Hcomm.md) | Overview of AICore-side point-to-point communication interface template, protocol capabilities and usage constraints. |
| [Init](./aicore/hcomm/Init.md) | Initialize the temporary workspace for Hcomm. |
| [ReadNbi](./aicore/hcomm/ReadNbi.md) | Submit a point-to-point read task via the specified channel. |
| [WriteNbi](./aicore/hcomm/WriteNbi.md) | Submit a point-to-point write task via the specified channel. |
| [WriteWithNotifyNbi](./aicore/hcomm/WriteWithNotifyNbi.md) | Submit a write task and write a remote notification value. |
| [AtomicFAA](./aicore/hcomm/AtomicFAA.md) | Submit a Fetch-and-add atomic operation task. |
| [AtomicCAS](./aicore/hcomm/AtomicCAS.md) | Submit a Compare-and-swap atomic operation task. |
| [Commit](./aicore/hcomm/Commit.md) | Explicitly submit pending communication tasks on the channel. |
| [Drain](./aicore/hcomm/Drain.md) | Wait for all communication tasks on the channel to complete. |

## Header File
```cpp
#include "hcomm/hcomm.h"
```

## Related Documents
- [Hcomm Usage Guide](../guide/hcomm_usage.md)
- [AIV Direct-driven URMA WriteNbi/ReadNbi Sample](../../../examples/hcomm_write_read_nbi/README_en.md)
