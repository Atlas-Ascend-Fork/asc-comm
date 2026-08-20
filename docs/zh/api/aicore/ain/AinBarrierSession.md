# ain_barrier_session

## 功能说明

头文件为：

```cpp
#include "ain/ain.h"
```

`AscendC::ain_barrier_session`是AICore侧基于AIN signal实现的team内同步会话。构造时绑定`ain`实例、通信team和barrier资源索引；调用`sync`时对team内其他rank发送signal，并等待其他rank对应signal达到阈值。

## 模板参数

```cpp
template <unsigned comm_engine_mask = AscendC::ain_mask_default>
class ain_barrier_session;
```

| 参数 | 说明 |
| --- | --- |
| `comm_engine_mask` | 通信引擎选择掩码，默认值为`ain_mask_default`。当前为预留参数。 |

## 构造函数

```cpp
__aicore__ inline ain_barrier_session(
    AscendC::ain<comm_engine_mask>* ain,
    AscendC::asccomm_team_handle team,
    uint32_t index);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `ain` | 输入 | `ain`实例指针。 |
| `team` | 输入 | 通信team句柄。 |
| `index` | 输入 | barrier资源索引，不同索引用于隔离不同barrier会话。 |

## 常用接口

| 接口 | 说明 |
| --- | --- |
| [sync](./Sync.md) | 执行team内barrier同步，支持无超时和带超时两种重载。 |

## 约束说明

- 调用前Host侧需完成team创建，并准备barrier使用的同步内存。
- `ain`不能为nullptr；`team`需要是有效的device侧句柄。
- `index`需要对应可用的barrier资源，不同并发barrier应使用不同`index`隔离。
- `sync`内部会通过底层asccomm原子加更新远端signal，并轮询本端signal等待其他rank到达。
- 当前只支持`comm_protocol_ubc_ctp`协议路径。
