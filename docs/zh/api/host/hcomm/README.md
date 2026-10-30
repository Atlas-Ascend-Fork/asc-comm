# Host Hcomm API

Host侧共享Jetty（通信通道底层共享的队列实体）多通道管理接口，提供`MultiChannelHandle`的创建与销毁能力，支持HCCL（Huawei Collective Communication Library，华为集合通信库）通信域和Hcomm Endpoint两种重载。

- [MakeMultiChannelHandle](./MakeMultiChannelHandle.md)
- [DestroyMultiChannelHandle](./DestroyMultiChannelHandle.md)
