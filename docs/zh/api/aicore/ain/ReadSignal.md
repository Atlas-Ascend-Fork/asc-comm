# ReadSignal

## 功能说明

读取本rank本地signal值，并按`bits`指定的低位宽度返回掩码后的结果。

`ReadSignal`会根据`team`、`signalWindow`和`signalOffset`解析本rank对应的signal地址，并通过device侧读操作读取`uint64_t` signal值。

## 函数原型

```cpp
__aicore__ inline uint64_t ReadSignal(
    AscendC::HcommTeamHandle team,
    AscendC::HcommWindowHandle signalWindow,
    size_t signalOffset,
    uint32_t bits = 64,
    AscendC::AinMemoryOrder order = AscendC::AIN_MEMORY_ORDER_RELAX) const;
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `team` | 输入 | 通信team句柄。 |
| `signalWindow` | 输入 | signal所在对称window句柄。 |
| `signalOffset` | 输入 | signal在对称window内的字节偏移。 |
| `bits` | 输入 | 读取低位bit数，取值范围为1-64，默认64。 |
| `order` | 输入 | signal读内存序，默认`AIN_MEMORY_ORDER_RELAX`。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `uint64_t` | 本rank本地signal低`bits`位掩码后的值。 |

## 约束说明

- 调用前Host侧需完成team创建及signal所在对称window注册。
- `team`和`signalWindow`需要是有效的device侧句柄。
- `signalOffset`对应地址需要落在本rank对应的signal window注册内存范围内。
- signal地址应按`uint64_t`访问要求准备。
- `bits`用于生成低位掩码；取值范围为1-64，当`bits`为64时读取完整64位signal值。
- 当前仅支持`AIN_MEMORY_ORDER_RELAX`。该内存序只保证signal原子读写及阈值检查，不保证signal操作前后普通数据访问的内存序。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。
