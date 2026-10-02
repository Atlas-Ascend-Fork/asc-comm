# AICPU Asccomm API 文档逐页检阅进度

本文记录 `docs/zh/api/aicpu/asccomm/` 下 23 个 API 文档的逐页检阅和确认修改进度。

- 进度：**6/23**
- 开始日期：2026-09-30
- 评审约束：[API_DOC_REVIEW_CONSTRAINTS.md](API_DOC_REVIEW_CONSTRAINTS.md)

## 图例

- `[ ]`：待检阅。
- `[x]`：已完成逐页检阅，且修改方案已由用户确认并落实。
- `[~]`：检阅中发现问题，方案待用户确认。

## 批量操作

- [x] [asccomm_batch_mode_start.md](../batch_operations/asccomm_batch_mode_start.md)
- [x] [asccomm_batch_mode_end.md](../batch_operations/asccomm_batch_mode_end.md)
- [x] [asccomm_batch_transfer_on_thread.md](../batch_operations/asccomm_batch_transfer_on_thread.md)

## 数据搬运

- [x] [asccomm_local_copy_on_thread.md](../data_movement/asccomm_local_copy_on_thread.md)
- [x] [asccomm_local_reduce_on_thread.md](../data_movement/asccomm_local_reduce_on_thread.md)
- [x] [asccomm_read_on_thread.md](../data_movement/asccomm_read_on_thread.md)
- [ ] [asccomm_read_reduce_on_thread.md](../data_movement/asccomm_read_reduce_on_thread.md)
- [ ] [asccomm_write_on_thread.md](../data_movement/asccomm_write_on_thread.md)
- [ ] [asccomm_write_reduce_on_thread.md](../data_movement/asccomm_write_reduce_on_thread.md)
- [ ] [asccomm_write_reduce_with_notify_on_thread.md](../data_movement/asccomm_write_reduce_with_notify_on_thread.md)
- [ ] [asccomm_write_with_notify_on_thread.md](../data_movement/asccomm_write_with_notify_on_thread.md)

## 同步与控制

- [ ] [asccomm_aclrt_notify_record_on_thread.md](../synchronization/asccomm_aclrt_notify_record_on_thread.md)
- [ ] [asccomm_aclrt_notify_wait_on_thread.md](../synchronization/asccomm_aclrt_notify_wait_on_thread.md)
- [ ] [asccomm_channel_drain_on_thread.md](../synchronization/asccomm_channel_drain_on_thread.md)
- [ ] [asccomm_channel_fence_on_thread.md](../synchronization/asccomm_channel_fence_on_thread.md)
- [ ] [asccomm_channel_notify_record_on_thread.md](../synchronization/asccomm_channel_notify_record_on_thread.md)
- [ ] [asccomm_channel_notify_wait_on_thread.md](../synchronization/asccomm_channel_notify_wait_on_thread.md)
- [ ] [asccomm_channel_notify_wait_on_thread_with_default_timeout.md](../synchronization/asccomm_channel_notify_wait_on_thread_with_default_timeout.md)
- [ ] [asccomm_set_notify_wait_timeout.md](../synchronization/asccomm_set_notify_wait_timeout.md)
- [ ] [asccomm_thread_notify_record_on_thread.md](../synchronization/asccomm_thread_notify_record_on_thread.md)
- [ ] [asccomm_thread_notify_wait_on_thread.md](../synchronization/asccomm_thread_notify_wait_on_thread.md)
- [ ] [asccomm_thread_notify_wait_on_thread_with_default_timeout.md](../synchronization/asccomm_thread_notify_wait_on_thread_with_default_timeout.md)
- [ ] [asccomm_thread_res_acquire_timeout.md](../synchronization/asccomm_thread_res_acquire_timeout.md)

## 已完成记录

| API 文档 | 完成日期 | 结果 |
| --- | --- | --- |
| [asccomm_batch_mode_start.md](../batch_operations/asccomm_batch_mode_start.md) | 2026-09-30 | 已按确认方案简化功能说明、收敛返回值、调整约束和示例注释。 |
| [asccomm_batch_mode_end.md](../batch_operations/asccomm_batch_mode_end.md) | 2026-09-30 | 已按确认方案简化功能说明、收敛返回值、明确无待提交任务时的行为，并同步示例注释。 |
| [asccomm_batch_transfer_on_thread.md](../batch_operations/asccomm_batch_transfer_on_thread.md) | 2026-10-02 | 保留原功能说明，更新句柄参数说明、返回值表和约束说明。 |
| [asccomm_local_copy_on_thread.md](../data_movement/asccomm_local_copy_on_thread.md) | 2026-10-02 | 更新Thread参数说明、返回值表，删除重复的4GiB约束，并简化调用示例。 |
| [asccomm_local_reduce_on_thread.md](../data_movement/asccomm_local_reduce_on_thread.md) | 2026-10-02 | 修正功能和参数说明，更新返回值表，删除重复的4GiB约束，并补充调用示例参数。 |
| [asccomm_read_on_thread.md](../data_movement/asccomm_read_on_thread.md) | 2026-10-02 | 简化功能说明，统一句柄参数表述，按代码路径补充返回值表，调整地址约束和调用示例。 |
