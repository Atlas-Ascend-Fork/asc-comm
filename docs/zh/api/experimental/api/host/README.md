# Host控制面API

Host控制面负责建立context、绑定instance、准备buffer/token、提交kernel和有界同步。
通信域、channel、通信内存和instance的创建由部署环境提供的HCOMM public接口完成。

## 接口列表

- [Context与资源](context.md)
- [Kernel启动](launch.md)
- [同步、超时与清理](synchronize.md)

## 基本顺序

1. 建立communicator。
2. 查询CCU instance。
3. 创建stream。
4. 调用`ccu_context_create`。
5. 准备带address、size、token的buffer view。
6. 调用`ccu_context_bind_instance`绑定部署环境提供的instance。
7. 使用`K<<<schd, instance, stream>>>(...)`异步提交。
8. 使用`ccu_synchronize`等待并处理错误。
9. 逆序销毁context、buffer、stream和communicator。
