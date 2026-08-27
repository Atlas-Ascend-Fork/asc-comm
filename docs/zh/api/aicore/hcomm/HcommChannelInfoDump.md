# HcommChannelInfoDump

## 功能说明
在`aclrtSynchronizeStream`返回错误后的host安全点，采集并打印通信通道诊断信息。没有记录到异常时也可以无条件调用，接口会直接返回成功且不打印。

## 函数原型
```cpp
int32_t HcommChannelInfoDump(AscendC::ChannelHandle channel);
```

## 参数说明
| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `channel` | 输入 | 算子持有的通信通道句柄。 |

## 返回值
| 返回值 | 说明 |
| --- | --- |
| `0` | dump成功或当前没有异常记录。 |
| `-1` | dump失败。 |

## 约束说明
- 必须在流同步失败后的host安全点调用。
- 接口内部会检查pending异常状态，调用方无需手动判断kernel是否出错。
- 第一次成功dump后会消费已记录的异常状态，后续重复调用会直接返回成功且不打印陈旧现场。
