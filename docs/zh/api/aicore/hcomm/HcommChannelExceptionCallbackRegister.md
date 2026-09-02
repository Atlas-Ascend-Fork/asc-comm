# HcommChannelExceptionCallbackRegister

## 功能说明
在launch前为目标kernel绑定异常回调。kernel触发AIC Error后，框架只记录异常状态，不在回调中做D2H、打印或再次launch。

## 函数原型
```cpp
int32_t HcommChannelExceptionCallbackRegister(const void *kernelSymbol);
```

## 参数说明
| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `kernelSymbol` | 输入 | 用于绑定异常回调的kernel符号。 |

## 返回值
| 返回值 | 说明 |
| --- | --- |
| `0` | 注册成功。 |
| `-1` | 注册失败。 |

## 约束说明
- 必须在launch目标kernel之前调用。
- 回调只记录异常状态，不在异常回调中执行D2H、打印或kernel launch操作。
- 如果不注册该接口，框架不记录异常现场，调用 dump 接口时每次都会直接执行采集。
