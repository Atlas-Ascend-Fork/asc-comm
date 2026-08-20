# ain

## 功能说明

头文件为：

```cpp
#include "ain/ain.h"
```

`AscendC::ain`是AICore侧单边通信接口模板，提供`put`/`get`/`signal`/`flush`等单边通信及同步能力。`ain`会根据通信team、对端rank和对称window句柄解析通信通道及GM地址，Kernel侧无需直接维护远端地址。

## 模板参数

```cpp
template <unsigned comm_engine_mask = AscendC::ain_mask_default>
class ain;
```

| 参数 | 说明 |
| --- | --- |
| `comm_engine_mask` | 通信引擎选择掩码，默认值为`ain_mask_default`。当前为预留参数。 |

## 构造函数

```cpp
__aicore__ inline ain(uint32_t context_index = 0);
```

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `context_index` | 输入 | 通信上下文索引，默认值为0。 |

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [put](./Put.md) | 将本端对称window中的数据写入对端对称window。 |
| [put_value](./PutValue.md) | 将立即数写入对端对称window。 |
| [get](./Get.md) | 从对端对称window读取数据到本端对称window。 |
| [flush](./Flush.md) | 等待team内所有peer通道上的通信任务完成。 |
| [flush_async](./FlushAsync.md) | 获取指定peer的通信通道句柄，用于后续异步等待。 |
| [wait](./Wait.md) | 等待指定通信通道上的任务完成。 |
| [signal](./Signal.md) | 对远端signal执行原子加操作。 |
| [read_signal](./ReadSignal.md) | 读取本端signal值。 |
| [wait_signal](./WaitSignal.md) | 等待本端signal达到指定阈值。 |

## 约束说明

- 调用前Host侧需完成team创建、对称window注册。
- 传入的`asccomm_team_handle`和`asccomm_window_handle`需要是有效的device侧句柄。
- 当前只支持`comm_protocol_ubc_ctp`协议路径。
- `put`、`put_value`、`get`和`signal`内部会初始化底层asccomm，调用时需要提供有效的`ain_descriptor_ubuf`。
