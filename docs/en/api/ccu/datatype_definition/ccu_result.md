# ccu_result

## Function Description

Header file: `#include <ccu/asc_ccu.h>`

`ccu_result` is the unified status code type of CCU APIs, used to indicate the execution result of an API call or kernel translation.

## Type Definition

`ccu_result` is declared as a scoped enumeration with an underlying type of `int32_t` in `ccu/ccu_api/ccu_types.h`:

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

## Enum Value Description

| Enum Value | Value | Description |
| --- | --- | --- |
| `CCU_SUCCESS` | 0 | Operation succeeded. |
| `CCU_E_PARA` | 1 | Invalid parameter. |
| `CCU_E_PTR` | 2 | Null pointer error; returned when the API is called outside the kernel function body. |
| `CCU_E_INTERNAL` | 4 | Internal error. |
| `CCU_E_NOT_SUPPORT` | 5 | Unsupported feature. |
| `CCU_E_NOT_FOUND` | 6 | Specified resource not found. |
| `CCU_E_UNAVAIL` | 7 | Resource unavailable. |
| `CCU_E_RUNTIME` | 15 | Runtime error. |
| `CCU_E_DRV_START` | 4096 | Start marker of the driver-layer error code range, not returned as an actual error code. |
| `CCU_E_DRV_INIT_FAILED` | 4097 | Driver initialization failed. |
| `CCU_E_DRV_BUSY` | 4098 | Driver busy. |
| `CCU_E_DRV_END` | 4224 | End marker of the driver-layer error code range, not returned as an actual error code. |
| `CCU_E_RESERVED` | 9216 | Start marker of the reserved error code range, not returned as an actual error code and reserved for future extension. |

## Constraints

- Access enumeration values with the `ccu_result::` prefix, for example `ccu_result::CCU_SUCCESS`.
- `CCU_E_DRV_START`, `CCU_E_DRV_END`, and `CCU_E_RESERVED` are error code range markers and are not returned as actual error codes.
- Data-plane APIs are called during kernel translation. Their return values indicate the translation-time validation result, and the translated instruction sequence is executed by the CCU hardware at run time.

## Example

```cpp
#include <ccu/asc_ccu.h>

using namespace asc::ccu;

__ccu_host__ void my_kernel(ccu_kernel_arg arg) {
    variable value;

    // Load the 0th kernel argument into value; arg_id is the kernel argument index.
    ccu_result ret = load_arg(value, 0);
    if (ret != ccu_result::CCU_SUCCESS) {
        return;
    }

    // Load data from address 0x80000000 into value.
    ret = load(0x80000000ULL, value);
    if (ret != ccu_result::CCU_SUCCESS) {
        return;
    }
}
```
