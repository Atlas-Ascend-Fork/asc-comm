# Hcomm Batch 接口遗留问题

本文记录单 Jetty 多对端 Batch 方案中尚未定稿的 Host 接口问题。当前实现先对齐
[asc-comm PR 97](https://gitcode.com/cann/asc-comm/pull/97) 中 `docs/hcomm_api_review.md` 的推荐方案，
遗留项经评审后再调整公开 ABI。

## 当前接口

```cpp
HcclResult MakeMultiChannelHandle(
    HcclComm comm,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

当前实现约定如下：

1. 内部固定使用 `COMM_ENGINE_AIV` 调用 `HcclChannelAcquireWithConfig`。
2. 内部启用 `HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE`，`sharedQueueTag` 不对外暴露。
3. `channels` 是长度为 `channelNum`、初始值全为 0 的输出数组，由
   `HcclChannelAcquireWithConfig` 填充。
4. Host 接口只校验所有 `channelDescs` 具有相同的 `localEndpoint`。协议支持范围由
   `HcclChannelAcquireWithConfig` 校验。返回的多个 Channel 对应不同对端，但共享同一个本端 Jetty。
5. `MultiChannelHandle` 指向连续的 Device `ChannelEntity[channelNum]`。数组顺序与
   `channelDescs`/`channels` 的顺序一致。

## 是否传入 CommEngine

待评审接口形态：

```cpp
HcclResult MakeMultiChannelHandle(
    HcclComm comm,
    CommEngine engine,
    const HcclChannelDesc* channelDescs,
    uint32_t channelNum,
    MultiChannelHandle* multiChannel);
```

当前暂不传入 `engine`，原因是共享 Jetty Batch 数据面当前只支持 AIV，传入其他值也只能返回不支持。
保留显式 `engine` 参数的价值是与 `HcclChannelAcquireWithConfig` 保持一致，并为后续其他通信引擎扩展 ABI。

待确认：

1. 该接口是否长期只服务 AICore/AIV Batch。如果是，保持内部固定 `COMM_ENGINE_AIV`。
2. 是否要求 Host API 与 HCOMM 控制面接口形态一致。如果是，增加 `CommEngine engine`，当前仅接受
   `COMM_ENGINE_AIV`。
3. 若未来支持其他 engine，`MultiChannelHandle` 的 Device 实体布局是否仍可复用 `ChannelEntity`。

## 是否传入 sharedQueueTag

HCOMM 共享 Channel 配置要求非空 `SHARED_QUEUE_TAG`，但评审文档的 `MakeMultiChannelHandle` 未暴露该参数。
当前实现使用内部 tag。

待确认：

1. 不同 `MakeMultiChannelHandle` 调用是否允许复用相同 `tag + endpointPair` 下的 Channel。
2. tag 应由调用者传入、由接口内部按通信域生成，还是每个 `MultiChannelHandle` 使用唯一 tag。
3. tag 仅用于 HCCM Channel 池复用，不应改变同一个 Endpoint 下多个 Channel 共享 Jetty 的基本语义。

## remoteIndex 语义

评审文档中的 Device 接口使用 `GetHandleRef(h, remoteIndex)`。当前连续数组按 `channelDescs` 的输入顺序填充，
因此 `remoteIndex` 实际表示数组下标，不一定等于全局 `rankId`。`MultiChannelHandle` 当前不携带
`channelNum`，Device 侧无法检查 `remoteIndex` 是否越界，首版由调用者保证索引合法。

待确认：

1. `remoteIndex` 是 `channelDescs` 下标还是全局 `rankId`。
2. 若要求等于全局 `rankId`，Host 侧是否应按 `rankDim` 分配数组，并为本 rank 或未建链 rank 保留空槽。
3. 是否允许同一远端 rank 存在多个 Channel；如允许，需要额外的 Channel 索引维度。

## GetHandleRef 不传入 remoteAddr

Device 接口为：

```cpp
BatchHandleRef GetHandleRef(BatchHandle handle, uint32_t remoteIndex);
```

`BatchHandleRef` 只绑定远端 Channel，不绑定某个 MR。`WriteNbi(ref, dst, src, len)` 使用已有的 `dst + len`
在该 Channel 的远端 MR 数组中动态查找并完成地址范围校验，然后使用匹配 MR 的 token 构造 WQE。

## 生命周期

`HcclChannelAcquireWithConfig` 返回的共享 Channel 由通信域管理。`DestroyMultiChannelHandle` 当前只释放连续
`ChannelEntity` 副本，不销毁原 Channel。调用者必须保证销毁句柄时没有 Kernel 正在使用该句柄，并在销毁
通信域前完成句柄清理。
