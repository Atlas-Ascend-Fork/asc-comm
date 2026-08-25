# 遗留问题

## `MakeMultiChannelHandleWithComm` 的 Tag 语义耦合

**状态：** 待设计

`MakeMultiChannelHandleWithComm` 当前将入参 `sharedQueueTag` 同时用于以下两类资源：

- `HcclChannelAcquireWithConfig` 的共享队列 Tag，用于按照通信域、Tag 和 Endpoint Pair 复用共享 Jetty Channel。
- `HcclEngineCtxCreate` 的上下文 Tag，用于在通信域和 `COMM_ENGINE_AIV` 下唯一索引 Device 侧 `MultiChannelHandle` 元数据。

两类 Tag 属于不同的内部命名空间，不会直接冲突，但生命周期和唯一性语义不同。共享队列 Tag 允许重复使用以复用或扩充 Channel；EngineCtx Tag 不允许重复创建。因此，当前实现实际限制同一通信域中的每个 `sharedQueueTag` 只能成功创建一个 `MultiChannelHandle`，削弱了共享 Channel 的复用能力。

### 当前约束

- 同一通信域内，每个 `sharedQueueTag` 仅用于一次成功调用。
- 重复调用可能已复用或创建 Channel，但随后因 EngineCtx Tag 重复而失败；Channel 最终由通信域统一清理。

### 建议方案

将两个概念拆分：保留可复用的 `sharedQueueTag`，并增加唯一的 `multiChannelTag` 专门管理 EngineCtx。需要同步评估公开接口兼容方案，并更新中英文 API 文档与失败路径测试。

完成后，相同 `sharedQueueTag` 应能配合不同 `multiChannelTag` 创建多个 `MultiChannelHandle`，而重复的 `multiChannelTag` 应返回明确错误。
