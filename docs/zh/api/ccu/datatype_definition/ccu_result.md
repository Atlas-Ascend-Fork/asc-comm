# ccu_result

## 功能说明

头文件为：`#include <ccu/asc_ccu.h>`

`ccu_result`是CCU接口的统一状态码类型，用于表示接口调用或Kernel翻译的执行结果。

## 类型定义

`ccu_result`在`ccu/ccu_api/ccu_types.h`中声明为作用域枚举（底层类型`int32_t`）：

```cpp
typedef enum class ccu_result : int32_t {
    CCU_SUCCESS = 0,
    CCU_E_PARA = 1,
    CCU_E_PTR = 2,
    CCU_E_INTERNAL = 4,
    CCU_E_NOT_SUPPORT = 5,
    CCU_E_NOT_FOUND = 6,
    CCU_E_UNAVAIL = 7,
    CCU_E_RUNTIME = 15,
    CCU_E_DRV_START = 4096,
    CCU_E_DRV_INIT_FAILED = 4097,
    CCU_E_DRV_BUSY = 4098,
    CCU_E_DRV_END = 4224,
    CCU_E_RESERVED = 9216
} ccu_result;
```

## 取值说明

| 枚举值 | 数值 | 说明 |
| --- | --- | --- |
| `CCU_SUCCESS` | 0 | 操作成功。 |
| `CCU_E_PARA` | 1 | 参数错误。 |
| `CCU_E_PTR` | 2 | 空指针错误；接口在Kernel函数体之外被调用时返回。 |
| `CCU_E_INTERNAL` | 4 | 内部错误。 |
| `CCU_E_NOT_SUPPORT` | 5 | 不支持的特性。 |
| `CCU_E_NOT_FOUND` | 6 | 未找到指定资源。 |
| `CCU_E_UNAVAIL` | 7 | 资源不可用。 |
| `CCU_E_RUNTIME` | 15 | 运行时错误。 |
| `CCU_E_DRV_START` | 4096 | 驱动层错误码段的起始标记，不作为实际错误码返回。 |
| `CCU_E_DRV_INIT_FAILED` | 4097 | 驱动初始化失败。 |
| `CCU_E_DRV_BUSY` | 4098 | 驱动忙。 |
| `CCU_E_DRV_END` | 4224 | 驱动层错误码段的结束标记，不作为实际错误码返回。 |
| `CCU_E_RESERVED` | 9216 | 保留错误码段的起始标记，不作为实际错误码返回，预留用于后续扩展。 |

## 约束说明

- 需通过`ccu_result::`前缀访问枚举值，例如`ccu_result::CCU_SUCCESS`。
- `CCU_E_DRV_START`、`CCU_E_DRV_END`、`CCU_E_RESERVED`为错误码段标记，不作为实际错误码返回。
- 数据面接口在Kernel翻译期调用，返回值表示该接口在翻译期的校验结果；执行期由CCU硬件执行翻译出的指令序列。

## 调用示例

```cpp
#include <ccu/asc_ccu.h>

using namespace asc::ccu;

__ccu_host__ void my_kernel(ccu_kernel_arg arg) {
    variable value;

    // 将Kernel第0个参数加载到value，arg_id为Kernel参数序号
    ccu_result ret = load_arg(value, 0);
    if (ret != ccu_result::CCU_SUCCESS) {
        return;
    }

    // 从地址0x80000000加载数据到value
    ret = load(0x80000000ULL, value);
    if (ret != ccu_result::CCU_SUCCESS) {
        return;
    }
}
```
