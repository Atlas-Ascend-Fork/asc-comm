# Introduction

<!-- md-trans-meta sourceCommit=e0aec142dc7ea9a47f1450a1f4a83ddab29157bc translatedAt=2026-11-04T10:24:23.435Z pushedAt=2026-11-09T06:47:55.001Z nl=2 -->

This section describes the synchronization and fence APIs of AI CPU Asccomm: Thread Notify is used for synchronization between threads, Channel Notify for synchronization between the two ends of a communication channel, Fence for ensuring the order of communication operations before and after the fence, and Drain for waiting until submitted tasks are complete.

## API List

- [asccomm_thread_notify_record_on_thread](asccomm_thread_notify_record_on_thread.md)
- [asccomm_thread_notify_wait_on_thread](asccomm_thread_notify_wait_on_thread.md)
- [asccomm_thread_notify_wait_on_thread_with_default_timeout](asccomm_thread_notify_wait_on_thread_with_default_timeout.md)
- [asccomm_channel_notify_record_on_thread](asccomm_channel_notify_record_on_thread.md)
- [asccomm_channel_notify_wait_on_thread](asccomm_channel_notify_wait_on_thread.md)
- [asccomm_channel_notify_wait_on_thread_with_default_timeout](asccomm_channel_notify_wait_on_thread_with_default_timeout.md)
- [asccomm_channel_fence_on_thread](asccomm_channel_fence_on_thread.md)
- [asccomm_channel_drain_on_thread](asccomm_channel_drain_on_thread.md)
