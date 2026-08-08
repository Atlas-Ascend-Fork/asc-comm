# WaitSignal

## 功能说明

阻塞等待本rank本地signal值达到指定阈值。

`WaitSignal`会根据`team`、`signalWindow`和`signalOffset`解析本rank对应的signal地址，循环读取低`bits`位掩码后的signal值，直到该值大于等于`least`。

## 函数原型

```cpp
__aicore__ inline void WaitSignal(
    AscendC::HcommTeamHandle team,
    AscendC::HcommWindowHandle signalWindow,
    size_t signalOffset,
    uint64_t least,
    uint32_t bits = 64,
    AscendC::AinMemoryOrder order = AscendC::AIN_MEMORY_ORDER_RELAX) const;
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `team` | 输入 | 通信team句柄。 |
| `signalWindow` | 输入 | signal所在对称window句柄。 |
| `signalOffset` | 输入 | signal在对称window内的字节偏移。 |
| `least` | 输入 | 等待阈值。 |
| `bits` | 输入 | 比较低位bit数，取值范围为1-64，默认64。 |
| `order` | 输入 | signal等待内存序，默认`AIN_MEMORY_ORDER_RELAX`。 |

## 返回值

无返回值。

## 约束说明

- 调用前Host侧需完成team创建及signal所在对称window注册。
- `team`和`signalWindow`需要是有效的device侧句柄。
- `signalOffset`对应地址需要落在本rank对应的signal window注册内存范围内。
- signal地址应按`uint64_t`访问要求准备。
- `bits`用于生成低位掩码；取值范围为1-64，当`bits`为64时比较完整64位signal值。
- 当前仅支持`AIN_MEMORY_ORDER_RELAX`。该内存序只保证signal原子读写及阈值检查，不保证signal操作前后普通数据访问的内存序。
- `WaitSignal`会持续轮询直到条件满足，调用方需要确保远端会更新该signal，避免Kernel永久等待。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。
