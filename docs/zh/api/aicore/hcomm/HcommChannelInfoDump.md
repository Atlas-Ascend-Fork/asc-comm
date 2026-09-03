# HcommChannelInfoDump

## 功能说明
在`aclrtSynchronizeStream`返回错误后的host侧安全点，采集、解析并打印通信通道诊断信息。接口可无条件调用，无论是否记录到异常；也可以传入一段调用方分配的字节缓冲区（`char *`）接收采集到的原始快照，供调用方自行分析。

原始快照以字节形式写入，不对外暴露内部数据结构。缓冲区所需字节数由[HcommChannelGetDumpInfoSize](./HcommChannelGetDumpInfoSize.md)返回，调用方据此分配内存。

## 函数原型
```cpp
int32_t HcommChannelInfoDump(AscendC::ChannelHandle channel, char *buf = nullptr, uint32_t len = 0);
```

## 参数说明
| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 算子持有的通信通道句柄。 |
| `buf` | 输出 | 可选，接收采集到的原始字节快照。若只需要默认打印，传`nullptr`。非空时长度必须不小于`HcommChannelGetDumpInfoSize()`。 |
| `len` | 输入 | `buf`的字节长度。`buf`为`nullptr`时忽略。 |

## 返回值
| 返回值 | 说明 |
| --- | --- |
| `0` | dump成功或当前没有异常记录。 |
| `-1` | dump失败。 |

## 约束说明
- 在host安全点调用，通常是流同步失败之后。
- 接口内部会检查pending异常状态：仅在已注册异常回调且无pending异常时跳过dump并直接返回成功，其余情况都会执行采集。
- dump成功后会清掉已记录的异常状态，后续重复调用会直接返回成功，不再打印旧现场；dump失败则保留异常状态，调用方可以重试。
- 当`buf`非空且长度足够时，接口先把采集到的原始字节写入该缓冲区，再执行默认打屏；跳过dump（无异常记录）时会把该缓冲区清零，避免读到上一次的残留字节。
- 快照以字节形式返回，不暴露公开结构体；缓冲区大小请通过`HcommChannelGetDumpInfoSize()`获取，不要硬编码。

## 打印内容说明
一次dump的全部输出会先在内部拼成一整块文本，最后一次性写出，避免多rank并发dump时逐行交错。输出依次包含：
- `Channel`：通道句柄、有效位、engine/protocol、buffer数量，以及`sqHead/sqTail/cqHead/cqTail`游标。
- `SqContext`/`CqContext`：队列上下文（`sqVa/depth/wqeSize`等）及`head/tail`。若`head`或`tail`读取失败，会输出独立的`ERROR`行，而不是在正常行上附加校验位。
- `WQE`：按`HcommUrmaSqeCtx`字段顺序打印全部字段，随后打印尾部的`SGE`；`opcode`为`WRITE_WITH_NOTIFY`时额外打印`notify`上下文。
- `CQE`：按`HcommUrmaJfcCqeCtx`字段顺序打印。

采样范围为未完成的任务：WQE取`[sqTail, sqHead)`，CQE取`[cqTail, cqHead)`（游标为自增计数，减法对回绕安全）。

> **说明（遗留问题确认）：** 若观察到`sqHead/sqTail/cqHead/cqTail`全为0，通常是kernel异常退出（如`trap()`）时未对`ChannelEntity`游标执行`dcci`刷cache，host从DRAM读回的是初始零值。这是kernel侧的cache一致性限制，DFX采集层无法绕过。此时未完成区间为空，dump不会打印误导性的WQE/CQE，属预期行为。
