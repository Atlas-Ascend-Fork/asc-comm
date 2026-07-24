# API Reference
## AICore Hcomm
| Document | Description |
| --- | --- |
| [Hcomm](./aicore/hcomm/en/Hcomm_en.md) | Overview of AICore-side point-to-point communication interface template, protocol capabilities and usage constraints. |
| [Init](./aicore/hcomm/en/Init_en.md) | Initialize the temporary workspace for Hcomm. |
| [ReadNbi](./aicore/hcomm/en/ReadNbi_en.md) | Submit a point-to-point read task via the specified channel. |
| [WriteNbi](./aicore/hcomm/en/WriteNbi_en.md) | Submit a point-to-point write task via the specified channel. |
| [WriteWithNotifyNbi](./aicore/hcomm/en/WriteWithNotifyNbi_en.md) | Submit a write task and write a remote notification value. |
| [AtomicFAA](./aicore/hcomm/en/AtomicFAA_en.md) | Submit a Fetch-and-add atomic operation task. |
| [AtomicCAS](./aicore/hcomm/en/AtomicCAS_en.md) | Submit a Compare-and-swap atomic operation task. |
| [Commit](./aicore/hcomm/en/Commit_en.md) | Explicitly submit pending communication tasks on the channel. |
| [Drain](./aicore/hcomm/en/Drain_en.md) | Wait for all communication tasks on the channel to complete. |

## Header File
```cpp
#include "hcomm/hcomm.h"
```

## Related Documents
- [Hcomm Usage Guide](../guide/en/hcomm_usage_en.md)
- [AIV Direct-driven URMA WriteNbi/ReadNbi Sample](../../examples/hcomm_write_read_nbi/README_en.md)
