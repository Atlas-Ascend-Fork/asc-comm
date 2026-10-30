# Hcomm类

AI Core侧点对点通信接口类，支持通过`ChannelHandle`提交读、写、写通知、原子操作和归约写任务，以及通过`BatchHandle`进行批量提交（`COMM_PROTOCOL_UB_CTP`协议支持单通道或多通道批量，`COMM_PROTOCOL_ROCE`协议支持单通道批量）。
