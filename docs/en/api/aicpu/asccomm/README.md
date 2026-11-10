# AI CPU Asccomm API Overview

<!-- md-trans-meta sourceCommit=5ed547c8af0b4fe59c907ddcd1d49348087706f5 translatedAt=2026-11-04T10:03:31.855Z pushedAt=2026-11-09T06:47:54.983Z nl=2 -->

[AI CPU](../../../guide/programming_model/abstract_hardware_architecture.md#communication-engine) Asccomm provides data-plane C APIs for communication threads and communication channels, used to describe local memory operations, cross-channel data transfer, notification synchronization, and batch submission operations. The public header file path after installation is `"aicpu/hcomm/asccomm_primitives.h"`. Component types such as `AsccommDataType`, `AsccommReduceOp`, and `AsccommBatchTransferDesc` are defined in `"aicpu/hcomm/asccomm_res_defs.h"`; the base definitions of `ThreadHandle` and `ChannelHandle` are provided by `hcomm_res_defs.h` in the runtime environment. After including `"aicpu/hcomm/asccomm_primitives.h"`, all the above types can be used directly.

## API Category

- [Local Operations](./local_operations/README.md): Local copy and local reduction operations on a thread.
- [Remote Operations](./remote_operations/README.md): Channel-based read/write, reduction read/write, write with notification, and batch descriptor transfer operations.
- [Synchronization and Barrier](./synchronization_and_barrier/README.md): Thread Notify and Channel Notify synchronization, as well as Fence and Drain operations.
- [Other Operations](./other_operations/README.md): Batch mode control, default timeout setting for Notify wait, and timeout setting for thread resource acquisition.
