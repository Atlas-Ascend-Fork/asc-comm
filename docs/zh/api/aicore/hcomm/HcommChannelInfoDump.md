# HcommChannelInfoDump

## 功能说明
在`aclrtSynchronizeStream`返回错误后的host侧安全点，采集、解析并打印通信通道诊断信息。接口可无条件调用，无论是否记录到异常；也可以传入输出缓冲区接收采集到的原始数据，供调用方自行分析。

## 函数原型
```cpp
int32_t HcommChannelInfoDump(AscendC::ChannelHandle channel, AscendC::HcommDfxChannelDumpData *dump = nullptr);
```

## 参数说明
| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 算子持有的通信通道句柄。 |
| `dump` | 输出 | 可选，接收采集到的原始数据。若只需要默认打印，传`nullptr`。 |

## 返回值
| 返回值 | 说明 |
| --- | --- |
| `0` | dump成功或当前没有异常记录。 |
| `-1` | dump失败。 |

## 约束说明
- 在host安全点调用，通常是流同步失败之后。
- 接口内部会检查pending异常状态：仅在已注册异常回调且无pending异常时跳过dump并直接返回成功，其余情况都会执行采集。
- dump成功后会清掉已记录的异常状态，后续重复调用会直接返回成功，不再打印旧现场；dump失败则保留异常状态，调用方可以重试。
- 当`dump`不为`nullptr`时，接口先把采集到的原始数据写入该缓冲区，再执行默认打屏。
