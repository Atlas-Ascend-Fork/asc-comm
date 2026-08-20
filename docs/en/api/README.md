# API Reference

## AICore Hcomm
| Document | Description |
| --- | --- |
| [Hcomm](./aicore/hcomm/Hcomm.md) | Overview of AICore-side point-to-point communication interface template, protocol capabilities and usage constraints. |
| [Init](./aicore/hcomm/Init.md) | Initialize the temporary workspace for Hcomm. |
| [read_nbi](./aicore/hcomm/ReadNbi.md) | Submit a point-to-point read task via the specified channel. |
| [write_nbi](./aicore/hcomm/WriteNbi.md) | Submit a point-to-point write task via the specified channel. |
| [write_with_notify_nbi](./aicore/hcomm/WriteWithNotifyNbi.md) | Submit a write task and write a remote notification value. |
| [atomic_faa](./aicore/hcomm/AtomicFAA.md) | Submit a Fetch-and-add atomic operation task. |
| [atomic_cas](./aicore/hcomm/AtomicCAS.md) | Submit a Compare-and-swap atomic operation task. |
| [commit](./aicore/hcomm/Commit.md) | Explicitly submit pending communication tasks on the channel. |
| [drain](./aicore/hcomm/Drain.md) | Wait for all communication tasks on the channel to complete. |

## AICore ain

### ain Class

| Document | Description |
| --- | --- |
| [ain](./aicore/ain/Ain.md) | Overview of the AICore-side AIN one-sided communication interface template, protocol capabilities and usage constraints. |
| [put](./aicore/ain/Put.md) | Write data from a local symmetric window to a peer symmetric window. |
| [put_value](./aicore/ain/PutValue.md) | Write an immediate value to a peer symmetric window. |
| [get](./aicore/ain/Get.md) | Read data from a peer symmetric window to a local symmetric window. |
| [flush](./aicore/ain/Flush.md) | Wait for communication tasks on all peer channels in the team to complete. |
| [flush_async](./aicore/ain/FlushAsync.md) | Get the communication channel handle of a specified peer for subsequent asynchronous waiting. |
| [wait](./aicore/ain/Wait.md) | Wait for tasks on a specified communication channel to complete. |
| [signal](./aicore/ain/Signal.md) | Perform an atomic add operation on a remote signal. |
| [read_signal](./aicore/ain/ReadSignal.md) | Read a local signal value. |
| [wait_signal](./aicore/ain/WaitSignal.md) | Wait until a local signal reaches the specified threshold. |

### ain_barrier_session Class

| Document | Description |
| --- | --- |
| [ain_barrier_session](./aicore/ain/AinBarrierSession.md) | Overview of the AICore-side AIN barrier synchronization session. |
| [sync](./aicore/ain/Sync.md) | Perform in-team barrier synchronization. |

## Header File
```cpp
#include "hcomm/hcomm.h"
#include "ain/ain.h"
```

## Related Documents
- [Hcomm Usage Guide](../guide/hcomm_usage.md)
- [AIV Direct-driven URMA write_nbi/read_nbi Sample](../../../examples/hcomm_write_read_nbi/README_en.md)
- [ain Basic Ring Sample](../../../examples/ain/basic_ring/README_en.md)
