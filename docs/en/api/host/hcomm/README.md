# Host Hcomm API

<!-- md-trans-meta sourceCommit=2d3bef9936ea5bb353382726a93a0544815f9e16 translatedAt=2026-11-05T01:46:44.349Z pushedAt=2026-11-09T06:49:22.791Z nl=2 -->

This section provides multi-channel management APIs for the shared Jetty (the queue entity shared by the underlying communication channels) on the host side. They provide the capability to create and destroy `MultiChannelHandle`, supporting two overloads: the HCCL (Huawei Collective Communication Library) communicator and the Hcomm endpoint.

- [MakeMultiChannelHandle](./MakeMultiChannelHandle.md)
- [DestroyMultiChannelHandle](./DestroyMultiChannelHandle.md)
