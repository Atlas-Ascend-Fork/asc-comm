# 预留接口

本章节列出的接口均为预留接口，后续可能存在变更，暂不支持开发者使用。

## Host Hcomm API

| 接口名 | 描述 |
| ---- | ---- |
| MakeMultiChannelHandle | 基于HCCL通信域或Hcomm Endpoint创建一组共享Jetty多通道及Device侧多通道句柄。 |
| DestroyMultiChannelHandle | 销毁MakeMultiChannelHandle（Endpoint版本）创建的多通道句柄及其资源。 |

## AICore Hcomm API

| 接口名 | 描述 |
| ---- | ---- |
| Init | 使用调用者提供的UB缓冲区初始化Hcomm通信工作区。 |
| MakeBatchHandle | 基于通信通道和UB缓冲区创建批量任务句柄，用于批量准备WQE。 |
| GetHandleRef | 获取多通道批量句柄中指定逻辑通道的执行批量句柄引用。 |
| WriteNbi | 通过指定通道异步将本端数据写入远端地址。 |
| WriteValueNbi | 通过指定通道异步向远端写入内联立即数，数据随WQE携带。 |
| WriteReduceNbi | 通过指定通道将本端数据写到远端并在远端执行规约。 |
| WriteWithNotifyNbi | 通过指定通道写远端数据，并在远端通知地址写入通知值。 |
| AtomicFAA | 对远端地址执行原子Fetch-and-add操作，并回读旧值。 |
| AtomicCAS | 对远端地址执行原子比较交换操作，并回读旧值。 |
| ReadNbi | 通过指定通道异步读取远端数据到本端地址。 |
| Commit | 通知服务端执行指定通道上已提交但未下发的通信任务。 |
| BatchCommit | 一次性复制并提交批量句柄中准备的全部WQE。 |
| Drain | 阻塞当前AI Core，等待指定通道上的通信任务全部完成。 |
| Lock | 获取通道的跨AI Core锁，保护通道状态的并发访问。 |
| Unlock | 刷新通道状态并释放跨AI Core锁。 |
