# Hcomm Class

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-04T09:22:27.896Z pushedAt=2026-11-09T06:47:54.960Z nl=2 -->

A point-to-point communication API class on the AI Core side. It supports submitting read, write, write-notify, atomic operation, and reduction-write tasks through `ChannelHandle`, and batch submission through `BatchHandle`. The `COMM_PROTOCOL_UB_CTP` protocol supports single-channel or multi-channel batching, while the `COMM_PROTOCOL_ROCE` protocol supports single-channel batching.
