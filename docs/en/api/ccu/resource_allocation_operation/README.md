# Introduction

<!-- md-trans-meta sourceCommit=5f8b41dd145ea0a95acdd2f2f2ea4995d681fdd0 translatedAt=2026-11-05T01:37:25.967Z pushedAt=2026-11-09T06:49:22.781Z nl=2 -->

This section provides resource classes in the CCU kernel, offering APIs for applying for and binding virtual resource handles, as well as APIs for assigning values to and performing operations on `variable` and `address`. A resource class is a C++ wrapper of a hardware resource. During programming, you operate on C++ objects, which correspond to the registers or storage units of the CCU at the underlying layer.

Resources can be acquired in the following ways:

- **Default construction**: Create a new object.
- **Array batch construction**: Create a batch of physically contiguous resources at once, meeting the requirement for contiguous resources in LoopGroup replication scenarios.
- **Scheduled construction**: Bind resources scheduled on the host side.
- **Reference channel reservation**: Reuse the shared slot reserved when the channel is established.

The acquisition methods supported by each resource class are summarized as follows:

| Resource Type | Programming Object | Acquisition Method |
| --- | --- | --- |
| General-purpose register | `variable` | Default construction: [variable()](variable.md)<br>Batch construction: [array\<variable\>](array.md)<br> Scheduled construction: [variable(var_handle, index)](variable.md)<br>Reference channel reservation: [get_res_by_channel](get_res_by_channel.md) |
| Synchronization register slot | `event` | Default construction: [event()](event.md)<br>Batch construction: [array\<event\>](array.md)<br> Scheduled construction: [event(acq_handle, index)](event.md) |
| On-chip cache (CCU Buffer, 4KB) | `ccu_buffer` | Default construction: [ccu_buffer()](ccu_buffer.md)<br>Batch construction: [array\<ccu_buffer\>](array.md) |
| Address register | `address` | Default construction: [address()](address.md) |
| Local address + token | `local_addr` | Default construction: [local_addr()](local_addr.md) |
| Remote address + token | `remote_addr` | Default construction: [remote_addr()](remote_addr.md) |

In addition to resource acquisition, [variable](variable.md) and [address](address.md) also provide assignment and operators: `variable` supports assignment, addition, and conditional comparison; `address` supports assignment and addition. See the corresponding documents for details.

## API List

- [variable](variable.md)
- [address](address.md)
- [event](event.md)
- [ccu_buffer](ccu_buffer.md)
- [local_addr](local_addr.md)
- [remote_addr](remote_addr.md)
- [array](array.md)
- [get_res_by_channel](get_res_by_channel.md)
