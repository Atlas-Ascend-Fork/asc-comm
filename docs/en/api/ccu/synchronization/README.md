# Introduction

<!-- md-trans-meta sourceCommit=2a95f77436441e03223b059000061595597749ee translatedAt=2026-11-05T01:45:04.300Z pushedAt=2026-11-09T06:49:22.789Z nl=2 -->

This section describes the synchronization APIs in the CCU kernel. When ordering dependencies must be established between asynchronous operations such as data transfers, the producer side writes a completion signal by calling the **record** API, and the consumer side blocks and waits for the signal to arrive by calling the **wait** API. The APIs are classified into two categories based on the scope of the signal:

| Type | Application Scenario | Producer-side API | Consumer-side API |
| --- | --- | --- | --- |
| Local event | Same kernel: waits for completion of asynchronous transfers initiated within this kernel. | [event_record](event_record.md) | [event_wait](event_wait.md) |
| Local notify | Cross-kernel within the same die: coordinates ordering between different kernels, paired by a string tag. | [local_notify_record](local_notify_record.md) | [local_notify_wait](local_notify_wait.md) |
| Remote notify | Cross-die (including cross-device and cross-node): passes signals through a channel. | [notify_record](notify_record.md) | [notify_wait](notify_wait.md) |

[write_variable_with_notify](write_variable_with_notify.md) is an extended API of remote notify. It combines "writing a remote variable value" and "triggering a remote notify" into an atomic operation, and is used in scenarios where a scalar value must be sent to the peer together with the completion signal.

## API List

- [event_record](event_record.md)
- [event_wait](event_wait.md)
- [local_notify_record](local_notify_record.md)
- [local_notify_wait](local_notify_wait.md)
- [notify_record](notify_record.md)
- [notify_wait](notify_wait.md)
- [write_variable_with_notify](write_variable_with_notify.md)
