# Sync

## 功能说明

执行`AinBarrierSession`绑定team内的barrier同步。

`Sync`会先向team内其他rank发送barrier signal，再等待其他rank写入本rank的barrier signal。无超时重载会一直等待直到同步完成；带超时重载在轮询次数超过`timeoutCycles`后返回失败。

## 函数原型

```cpp
template <typename DescriptorUbuf = AscendC::AinDescriptorUbuf>
__aicore__ inline void Sync(
    AscendC::AinMemoryOrder order,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

```cpp
template <typename DescriptorUbuf = AscendC::AinDescriptorUbuf>
__aicore__ inline int32_t Sync(
    AscendC::AinMemoryOrder order,
    uint64_t timeoutCycles,
    const DescriptorUbuf& ubuf = DescriptorUbuf{});
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `order` | 输入 | barrier signal等待内存序，当前支持`AIN_MEMORY_ORDER_RELAX`。 |
| `timeoutCycles` | 输入 | 最大轮询次数，仅带超时重载使用。 |
| `ubuf` | 输入 | 底层Hcomm使用的UBuf临时工作区描述。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `DescriptorUbuf` | UBuf临时工作区描述类型，默认`AinDescriptorUbuf`。 |

## 返回值

无超时重载无返回值。

带超时重载返回值如下：

| 返回值 | 说明 |
| --- | --- |
| `0` | 同步成功。 |
| `-1` | 等待超过`timeoutCycles`，同步失败。 |

## 约束说明

- 调用前需要先构造有效的`AinBarrierSession`。
- 调用前Host侧需完成team创建，并准备barrier使用的同步内存。
- `ubuf.addr`和`ubuf.bytes`需要提供可供底层Hcomm初始化使用的UBuf临时工作区；`DescriptorUbuf`当前必须提供不小于512B的UBuf临时工作区。
- 当前仅支持`AIN_MEMORY_ORDER_RELAX`。该内存序只保证signal原子读写及阈值检查，不保证signal操作前后普通数据访问的内存序。
- 无超时重载会持续轮询直到所有其他rank到达barrier，调用方需要保证team内rank对称调用，避免Kernel永久等待。
- 带超时重载的`timeoutCycles`表示轮询次数，返回`-1`时只表示本rank等待超时，不会回滚已经发出的signal。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。
