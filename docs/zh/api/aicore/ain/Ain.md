# Ain

## 功能说明

头文件为：

```cpp
#include "ain/ain.h"
```

`AscendC::Ain`是AICore侧单边通信接口模板，提供`Put`/`Get`/`Signal`/`Flush`等单边通信及同步能力。`Ain`会根据通信team、对端rank和对称window句柄解析通信通道及GM地址，Kernel侧无需直接维护远端地址。

## 模板参数

```cpp
template <unsigned CommEngineMask = AscendC::AIN_MASK_DEFAULT>
class Ain;
```

| 参数 | 说明 |
| --- | --- |
| `CommEngineMask` | 通信引擎选择掩码，默认值为`AIN_MASK_DEFAULT`。当前为预留参数。 |

## 构造函数

```cpp
__aicore__ inline Ain(uint32_t contextIndex = 0);
```

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `contextIndex` | 输入 | 通信上下文索引，默认值为0。 |

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [Put](./Put.md) | 将本端对称window中的数据写入对端对称window。 |
| [PutValue](./PutValue.md) | 将立即数写入对端对称window。 |
| [Get](./Get.md) | 从对端对称window读取数据到本端对称window。 |
| [Flush](./Flush.md) | 等待team内所有peer通道上的通信任务完成。 |
| [FlushAsync](./FlushAsync.md) | 获取指定peer的通信通道句柄，用于后续异步等待。 |
| [Wait](./Wait.md) | 等待指定通信通道上的任务完成。 |
| [Signal](./Signal.md) | 对远端signal执行原子加操作。 |
| [ReadSignal](./ReadSignal.md) | 读取本端signal值。 |
| [WaitSignal](./WaitSignal.md) | 等待本端signal达到指定阈值。 |

## 约束说明

- 调用前Host侧需完成team创建、对称window注册。
- 传入的`HcommTeamHandle`和`HcommWindowHandle`需要是有效的device侧句柄。
- 当前只支持`COMM_PROTOCOL_UBC_CTP`协议路径。
- `Put`、`PutValue`、`Get`和`Signal`内部会初始化底层Hcomm，调用时需要提供有效的`AinDescriptorUbuf`。
- 使用`AIN_COMMIT_DELAYED`时，连续调用次数不得超过底层SQ深度（`sqDepth`），需在SQ耗尽前通过`AIN_COMMIT_IMMED`提交积攒的任务，否则后续任务将因SQ溢出而失败。
- 批量提交场景（多次`AIN_COMMIT_DELAYED` + 最后一次`AIN_COMMIT_IMMED`）下，仅最后一次提交应产生CQE。
