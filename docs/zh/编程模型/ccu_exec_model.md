# CCU（A5）执行模型

本文介绍 CCU Kernel 执行过程中的顺序与同步机制，以及这些机制对通信内存可见性和复用时机提供的保证。内存类型、远端可见性和数据搬运路径参见[《CCU（A5）通信内存模型》](./ccu_memory_model_v1.0.md)。

## 同步与通信内存一致性

CCU 执行数据搬运指令时，搬运操作异步完成。指令开始执行不表示数据已经搬运完成，算子需要通过 Event 和 Notify 建立本端完成依赖与跨 die 消费依赖。

### 本端完成同步

数据搬运原语的末位参数包含 `event` 和 `mask`。搬运完成后，硬件自动置位对应的 `event[mask]`；Kernel 通过 [`EventWait`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/data_plane_api/ccu/synchronization/EventWait.md) 等待该状态位，再读取目的内存或复用源内存。搬运完成事件由硬件产生，不需要为同一次搬运额外调用 `EventRecord`。

同一 die 的不同 CCU Kernel 之间需要建立顺序时，可使用 [`LocalNotifyRecord`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/data_plane_api/ccu/synchronization/LocalNotifyRecord.md) 和 [`LocalNotifyWait`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/data_plane_api/ccu/synchronization/LocalNotifyWait.md)，并使用相同的 `notifyTag` 和 `mask` 完成配对。

### 跨 die 同步

[`NotifyRecord`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/data_plane_api/ccu/synchronization/NotifyRecord.md) 通过 Channel 置位对端 Notify，[`NotifyWait`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/data_plane_api/ccu/synchronization/NotifyWait.md) 阻塞当前执行序列，直到对应 Notify 到达。生产端的 `remoteNotifyIdx` 必须与消费端的 `localNotifyIdx` 相同，两端使用的 `mask` 也必须一致。`NotifyWait` 可以先进入等待状态，后续由匹配的 `NotifyRecord` 解除等待；若 Record 未发起或索引、mask 不匹配，Wait 将一直阻塞。

跨 die 传递标量元数据时，可使用 [`WriteVariableWithNotify`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/data_plane_api/ccu/synchronization/WriteVariableWithNotify.md)。该原语保证对端 Variable 先更新、对应 Notify 后到达，因此消费端可以在 `NotifyWait` 返回后读取新值。该保证只覆盖本次 Variable 写入，不表示此前任意 HBM 写入已经完成。

### 数据发布与消费

跨 die 写入后由对端消费数据时，应建立以下依赖：

1. 生产端发起数据搬运，并通过 `EventWait` 等待搬运完成。
2. 生产端通过 `NotifyRecord` 向消费端发布数据就绪信号。
3. 消费端通过 `NotifyWait` 等待该信号，再读取或处理目的数据。

`EventWait` 只确认对应数据搬运操作完成，不表示对端已经开始或完成消费。`NotifyRecord` 和 `NotifyWait` 只为匹配的 Channel、Notify 索引和 mask 建立跨 die 依赖，不为其他 Channel 或未建立依赖的操作提供顺序保证。

### 内存使用边界

| 边界 | 满足条件 | 满足后的行为 |
| --- | --- | --- |
| 源内存可复用 | 对源内存的异步读取已完成，并通过对应 `EventWait` 建立完成依赖。 | 可以修改、覆盖或释放源内存。 |
| 本端目的数据可访问 | 以本端内存为目的的异步操作已完成，并通过对应 `EventWait` 建立完成依赖。 | 当前 Kernel 可以读取或继续处理本端目的数据。 |
| 对端目的数据可消费 | 对端内存写入已完成，且消费端通过匹配的 `NotifyWait` 建立跨 die 依赖。 | 对端消费者可以读取或处理目的数据。 |
| 目的内存可复用 | 所有读取或处理该数据的消费者均已完成；跨端场景下由消费端反向通知生产端。 | 可以覆盖或释放目的内存。 |

异步操作完成前，源内存和目的内存必须保持有效，生产者不得改写仍在读取的源区域，消费者不得访问仍在写入的目的区域。若生产端还需要确认对端已经完成消费，消费端必须在处理结束后反向发送 Notify，不能仅根据生产端的 `EventWait` 或首次数据就绪 Notify 推导消费已经完成。
