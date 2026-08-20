# sync

## 功能说明

执行`ain_barrier_session`绑定team内的barrier同步。

`sync`会先向team内其他rank发送barrier signal，再等待其他rank写入本rank的barrier signal。无超时重载会一直等待直到同步完成；带超时重载在轮询次数超过`timeout_cycles`后返回失败。

## 函数原型

```cpp
template <typename descriptor_ubuf_t = AscendC::ain_descriptor_ubuf>
__aicore__ inline void sync(
    AscendC::ain_memory_order order,
    const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});
```

```cpp
template <typename descriptor_ubuf_t = AscendC::ain_descriptor_ubuf>
__aicore__ inline int32_t sync(
    AscendC::ain_memory_order order,
    uint64_t timeout_cycles,
    const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `order` | 输入 | barrier signal等待内存序，当前支持`ain_memory_order_relax`。 |
| `timeout_cycles` | 输入 | 最大轮询次数，仅带超时重载使用。 |
| `ubuf` | 输入 | 底层asccomm使用的UBuf临时工作区描述。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `descriptor_ubuf_t` | UBuf临时工作区描述类型，默认`ain_descriptor_ubuf`。 |

## 返回值

无超时重载无返回值。

带超时重载返回值如下：

| 返回值 | 说明 |
| --- | --- |
| `0` | 同步成功。 |
| `-1` | 等待超过`timeout_cycles`，同步失败。 |

## 约束说明

- 调用前需要先构造有效的`ain_barrier_session`。
- 调用前Host侧需完成team创建，并准备barrier使用的同步内存。
- `ubuf.addr`和`ubuf.bytes`需要提供可供底层asccomm初始化使用的UBuf临时工作区；`descriptor_ubuf_t`当前必须提供不小于512B的UBuf临时工作区。
- 当前仅支持`ain_memory_order_relax`。该内存序只保证signal原子读写及阈值检查，不保证signal操作前后普通数据访问的内存序。
- 无超时重载会持续轮询直到所有其他rank到达barrier，调用方需要保证team内rank对称调用，避免Kernel永久等待。
- 带超时重载的`timeout_cycles`表示轮询次数，返回`-1`时只表示本rank等待超时，不会回滚已经发出的signal。
- 当前只支持`comm_protocol_ubc_ctp`协议路径。
