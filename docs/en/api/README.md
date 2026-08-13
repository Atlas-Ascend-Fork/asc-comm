# API Reference

## AICore Hcomm
| Document | Description |
| --- | --- |
| [Hcomm](./aicore/hcomm/Hcomm.md) | Overview of AICore-side point-to-point communication interface template, protocol capabilities and usage constraints. |
| [Init](./aicore/hcomm/Init.md) | Initialize the temporary workspace used by ordinary Hcomm interfaces. |
| [MakeBatchHandle](./aicore/hcomm/MakeBatchHandle.md) | Create a batch handle and bind its UB buffer for batched WQEs. |
| [ReadNbi](./aicore/hcomm/ReadNbi.md) | Submit an ordinary read task or prepare a read WQE in a BatchHandle. |
| [WriteNbi](./aicore/hcomm/WriteNbi.md) | Submit an ordinary write task or prepare a write WQE in a BatchHandle. |
| [WriteWithNotifyNbi](./aicore/hcomm/WriteWithNotifyNbi.md) | Submit an ordinary write-with-notify task or prepare one in a BatchHandle. |
| [AtomicFAA](./aicore/hcomm/AtomicFAA.md) | Submit a Fetch-and-add atomic operation task. |
| [AtomicCAS](./aicore/hcomm/AtomicCAS.md) | Submit a Compare-and-swap atomic operation task. |
| [Commit](./aicore/hcomm/Commit.md) | Explicitly submit pending communication tasks on the channel. |
| [BatchCommit](./aicore/hcomm/BatchCommit.md) | Submit all WQEs prepared in a BatchHandle. |
| [Drain](./aicore/hcomm/Drain.md) | Wait for tasks submitted through an ordinary channel or BatchHandle to complete. |

## AICore Ain

### Ain Class

| Document | Description |
| --- | --- |
| [Ain](./aicore/ain/Ain.md) | Overview of the AICore-side AIN one-sided communication interface template, protocol capabilities and usage constraints. |
| [Put](./aicore/ain/Put.md) | Write data from a local symmetric window to a peer symmetric window. |
| [PutValue](./aicore/ain/PutValue.md) | Write an immediate value to a peer symmetric window. |
| [Get](./aicore/ain/Get.md) | Read data from a peer symmetric window to a local symmetric window. |
| [Flush](./aicore/ain/Flush.md) | Wait for communication tasks on all peer channels in the team to complete. |
| [FlushAsync](./aicore/ain/FlushAsync.md) | Get the communication channel handle of a specified peer for subsequent asynchronous waiting. |
| [Wait](./aicore/ain/Wait.md) | Wait for tasks on a specified communication channel to complete. |
| [Signal](./aicore/ain/Signal.md) | Perform an atomic add operation on a remote signal. |
| [ReadSignal](./aicore/ain/ReadSignal.md) | Read a local signal value. |
| [WaitSignal](./aicore/ain/WaitSignal.md) | Wait until a local signal reaches the specified threshold. |

### AinBarrierSession Class

| Document | Description |
| --- | --- |
| [AinBarrierSession](./aicore/ain/AinBarrierSession.md) | Overview of the AICore-side AIN barrier synchronization session. |
| [Sync](./aicore/ain/Sync.md) | Perform in-team barrier synchronization. |

## Header File
```cpp
#include "hcomm/hcomm.h"
#include "ain/ain.h"
```

## Related Documents
- [Hcomm Usage Guide](../guide/hcomm_usage.md)
- [AIV Direct-driven URMA WriteNbi/ReadNbi Sample](../../../examples/hcomm_write_read_nbi/README_en.md)
- [Ain Basic Ring Sample](../../../examples/ain/basic_ring/README_en.md)
