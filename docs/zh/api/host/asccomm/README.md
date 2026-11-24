# Host CommRW API

Host侧共享Jetty（通信通道底层共享的队列实体）多通道管理接口，提供`multi_channel_handle`的创建与销毁能力，支持HCCL（Huawei Collective Communication Library，华为集合通信库）通信域和Hcomm Endpoint两种重载。

- [make_multi_channel_handle](./make_multi_channel_handle.md)
- [destroy_multi_channel_handle](./destroy_multi_channel_handle.md)
