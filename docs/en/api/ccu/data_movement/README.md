# Introduction

<!-- md-trans-meta sourceCommit=2a95f77436441e03223b059000061595597749ee translatedAt=2026-11-04T10:34:38.902Z pushedAt=2026-11-09T06:47:55.010Z nl=2 -->

This section describes the data movement APIs in the CCU kernel, covering local data movement and remote data movement. Remote data movement supports optional in-transit reduction.

All data movement APIs are asynchronous. When the hardware completes a movement, it automatically sets the flag bit corresponding to `mask` in `event` to 1, and the downstream waits for the completion signal through `event_wait`. The APIs are classified into the following two categories by data path:

| Type | Application Scenario | API |
| --- | --- | --- |
| Local operation | Copy and reduction between local on-chip memory and local on-chip memory, or between local on-chip memory and local CCU Buffer | [local_copy](local_copy.md), [local_reduce](local_reduce.md) |
| Cross-rank operation | Read and write data between local and remote on-chip memory over a channel, with optional reduction | [read](read.md), [read_reduce](read_reduce.md), [write](write.md), [write_reduce](write_reduce.md) |

Cross-rank operations require that all `ChannelHandle` objects belong to the same die, which is validated by the framework.

## API List

- [local_copy](local_copy.md)
- [local_reduce](local_reduce.md)
- [read](read.md)
- [read_reduce](read_reduce.md)
- [write](write.md)
- [write_reduce](write_reduce.md)
