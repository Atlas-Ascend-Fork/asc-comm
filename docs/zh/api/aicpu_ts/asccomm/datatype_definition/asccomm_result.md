# asccomm_result

## 功能说明

头文件路径：`"aicpu_ts/asccomm/asccomm_result.h"`。

`asccomm_result`是AICPU_TS Asccomm数据面接口的统一状态码枚举，用于表示接口调用结果。

## 类型定义

`asccomm_result`在`include/aicpu_ts/asccomm/asccomm_result.h`中声明为无作用域枚举：

```cpp
typedef enum {
    ASCCOMM_SUCCESS = 0,
    ASCCOMM_E_PARA = 1,
    ASCCOMM_E_PTR = 2,
    ASCCOMM_E_MEMORY = 3,
    ASCCOMM_E_INTERNAL = 4,
    ASCCOMM_E_NOT_SUPPORT = 5,
    ASCCOMM_E_NOT_FOUND = 6,
    ASCCOMM_E_UNAVAIL = 7,
    ASCCOMM_E_SYSCALL = 8,
    ASCCOMM_E_TIMEOUT = 9,
    ASCCOMM_E_OPEN_FILE_FAILURE = 10,
    ASCCOMM_E_TCP_CONNECT = 11,
    ASCCOMM_E_ROCE_CONNECT = 12,
    ASCCOMM_E_TCP_TRANSFER = 13,
    ASCCOMM_E_ROCE_TRANSFER = 14,
    ASCCOMM_E_RUNTIME = 15,
    ASCCOMM_E_DRV = 16,
    ASCCOMM_E_PROFILING = 17,
    ASCCOMM_E_CCE = 18,
    ASCCOMM_E_NETWORK = 19,
    ASCCOMM_E_AGAIN = 20,
    ASCCOMM_E_REMOTE = 21,
    ASCCOMM_E_SUSPENDING = 22,
    ASCCOMM_E_OPRETRY_FAIL = 23,
    ASCCOMM_E_OOM = 24,
    ASCCOMM_E_IN_STATUS = 1041,
    ASCCOMM_E_RESERVED
} asccomm_result;
```

## 取值说明

| 枚举值 | 数值 | 说明 |
| --- | --- | --- |
| `ASCCOMM_SUCCESS` | 0 | 操作成功。 |
| `ASCCOMM_E_PARA` | 1 | 参数错误。 |
| `ASCCOMM_E_PTR` | 2 | 空指针错误。 |
| `ASCCOMM_E_MEMORY` | 3 | 内存错误。 |
| `ASCCOMM_E_INTERNAL` | 4 | 内部错误。 |
| `ASCCOMM_E_NOT_SUPPORT` | 5 | 不支持的特性。 |
| `ASCCOMM_E_NOT_FOUND` | 6 | 未找到指定资源。 |
| `ASCCOMM_E_UNAVAIL` | 7 | 资源不可用。 |
| `ASCCOMM_E_SYSCALL` | 8 | 系统接口调用错误。 |
| `ASCCOMM_E_TIMEOUT` | 9 | 超时。 |
| `ASCCOMM_E_OPEN_FILE_FAILURE` | 10 | 文件打开失败。 |
| `ASCCOMM_E_TCP_CONNECT` | 11 | TCP连接建立失败。 |
| `ASCCOMM_E_ROCE_CONNECT` | 12 | RoCE连接建立失败。 |
| `ASCCOMM_E_TCP_TRANSFER` | 13 | TCP传输失败。 |
| `ASCCOMM_E_ROCE_TRANSFER` | 14 | RoCE传输失败。 |
| `ASCCOMM_E_RUNTIME` | 15 | 运行时接口调用错误。 |
| `ASCCOMM_E_DRV` | 16 | 驱动接口调用错误。 |
| `ASCCOMM_E_PROFILING` | 17 | Profiling接口调用错误。 |
| `ASCCOMM_E_CCE` | 18 | CCE接口调用错误。 |
| `ASCCOMM_E_NETWORK` | 19 | 网络接口调用错误。 |
| `ASCCOMM_E_AGAIN` | 20 | 需要重试。 |
| `ASCCOMM_E_REMOTE` | 21 | 远端完成队列元素错误。 |
| `ASCCOMM_E_SUSPENDING` | 22 | 通信域挂起错误。 |
| `ASCCOMM_E_OPRETRY_FAIL` | 23 | 重试约束错误。 |
| `ASCCOMM_E_OOM` | 24 | 内存不足。 |
| `ASCCOMM_E_IN_STATUS` | 1041 | 预留错误码，当前Asccomm数据面接口不返回该值。 |
| `ASCCOMM_E_RESERVED` | 1042 | 保留错误码，不表示当前定义的具体错误类型。 |

## 约束说明

- 接口函数返回类型为`int32_t`，枚举常量按整数值参与比较；调用方可直接比较返回值，无需先将返回值转换为`asccomm_result`。
- 具体接口实际可返回的错误码由该接口的返回值说明决定，并非所有接口都会返回上表中的全部错误码。
- `ASCCOMM_E_RESERVED`为保留错误码；未在上表中明确定义数值的后续扩展错误码，以实际发布头文件为准。

## 调用示例

```cpp
#include "aicpu_ts/asccomm/asccomm_primitives.h"
#include "aicpu_ts/asccomm/asccomm_result.h"

// thread为控制面传入的有效ThreadHandle资源句柄；channel为控制面传入的有效ChannelHandle资源句柄。
int32_t ret = asccomm_channel_fence_on_thread(thread, channel);
if (ret != ASCCOMM_SUCCESS) {
    return ret;
}
```
