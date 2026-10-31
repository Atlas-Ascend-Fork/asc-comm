# 同步

本节提供kernel内的event和notify同步接口。异步搬运完成后设置event signal；跨rank协作使用notify signal。Host侧任务完成等待见 [Host同步API](../host/synchronize.md)。

## 接口列表

- [Event记录](event_record.md)
- [Event等待](event_wait.md)
- [Notify记录](notify_record.md)
- [Notify等待](notify_wait.md)
- [带Notify的Variable写入](write_variable_with_notify.md)
