# Reserved APIs

<!-- md-trans-meta sourceCommit=eea3a3c2f730105feffc016d6ebf7334a7a99572 translatedAt=2026-10-26T06:55:33.624Z pushedAt=2026-10-26T07:32:09.929Z nl=2 -->

All APIs listed in this section are reserved and may be changed in the future. They are not supported for developer use at present.

## Host Hcomm API

| API | Description |
| ---- | ---- |
| MakeMultiChannelHandle | Creates a group of shared Jetty multi-channels and device-side multi-channel handles based on an HCCL communicator or Hcomm endpoint. |
| DestroyMultiChannelHandle | Destroys the multi-channel handles and their resources created by **MakeMultiChannelHandle** (endpoint version). |

## AI Core Hcomm API

| API | Description |
| ---- | ---- |
| Init | Initializes the Hcomm communication workspace using the UB buffer provided by the caller. |
| MakeBatchHandle | Creates a batch task handle based on the communication channel and UB buffer, for batch preparation of WQEs. |
| GetHandleRef | Obtains the execution batch handle reference of the specified logical channel in the multi-channel batch handle. |
| WriteNbi | Asynchronously writes local data to a remote address by specifying a channel. |
| WriteValueNbi | Asynchronously writes an inline immediate value to the remote end by specifying a channel, with the data carried in the WQE. |
| WriteReduceNbi | Writes local data to the remote end by specifying a channel and performs reduction on the remote end. |
| WriteWithNotifyNbi | Writes remote data by specifying a channel and writes a notification value to the remote notification address. |
| AtomicFAA | Performs an atomic fetch-and-add operation on a remote address and reads back the old value. |
| AtomicCAS | Performs an atomic compare-and-swap operation on a remote address and reads back the old value. |
| ReadNbi | Asynchronously reads remote data to a local address by specifying a channel. |
| Commit | Notifies the server to execute the communication tasks that have been committed but not yet issued on the specified channel. |
| BatchCommit | Copies and commits all prepared WQEs in the batch handle at once. |
| Drain | Blocks the current AI Core and waits for all communication tasks on the specified channel to complete. |
| Lock | Acquires the cross-AI Core lock of the channel to protect concurrent access to the channel state. |
| Unlock | Flushes the channel state and releases the cross-AI Core lock. |
