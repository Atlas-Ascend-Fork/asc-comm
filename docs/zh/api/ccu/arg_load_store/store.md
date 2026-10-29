# store

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件为：`#include "ccu/hcomm/ccu_primitives.hpp"`

在CCU kernel内将一个或多个variable的`uint64_t`值写入片上内存地址。

为`load`的反向操作，具体支持的数据通路为：

- 单个variable -> 立即数地址
- variable数组 -> 立即数地址
- 单个variable -> variable地址（间接寻址）
- variable数组 -> variable地址（间接寻址）

立即数地址在注册阶段确定，执行阶段不可变；variable地址在运行期从硬件寄存器读出。

## 函数原型

- 单个variable -> 立即数地址

    ```cpp
    CcuResult store(uint64_t addr, variable v);
    ```

- variable数组 -> 立即数地址

    ```cpp
    CcuResult store(uint64_t addr, array<variable>& v_arr, uint32_t num);
    ```

- 单个variable -> variable地址（间接寻址）

    ```cpp
    CcuResult store(variable addr_var, variable v);
    ```

- variable数组 -> variable地址（间接寻址）

    ```cpp
    CcuResult store(variable addr_var, array<variable>& v_arr, uint32_t num);
    ```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| addr | 输入 | 立即数片上内存目标地址（`uint64_t`，立即数地址通路）。须为CCU可访问的物理地址或经token化的VA，在注册阶段确定，执行阶段不可变。 |
| addr_var | 输入 | 地址variable（`variable`，variable地址通路）。运行期该variable中存储的值被用作片上内存目标地址，须已赋有效地址值。 |
| v | 输入 | 源variable（单个variable通路）。运行期将该variable的值写入片上内存目标地址处。 |
| v_arr | 输入 | 源variable数组首元素（variable数组通路）。须通过`ccu::array<variable>`申请以保证物理连续。 |
| num | 输入 | 存储的`uint64_t`元素个数（variable数组通路）。须满足`1 ≤ num ≤ v_arr.size()`；`num>1`时将`v_arr[0]`至`v_arr[num-1]`依次写入起始地址开始、按8字节步长的片上内存位置。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PARA` | 参数错误：`num`为0或大于`v_arr.size()`；或`num>1`时`v_arr`元素物理不连续。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |
| `CCU_E_NOT_FOUND` | 传入的`v`/`v_arr`句柄（或`v_arr[1..num-1]`的相邻句柄）未在当前kernel注册。 |

## 约束说明

- `addr`须为CCU可访问的物理地址或经token化的VA，直接传入未经token化的进程虚拟地址将触发驱动错误。
- `num`须满足`1 ≤ num ≤ v_arr.size()`。本接口在访问`v_arr[0]`之前完成该校验，传入0或超过`v_arr.size()`时返回`CCU_E_PARA`，不会触达底层接口；空`array`（`v_arr.size()`为0）传入任意`num`同样返回`CCU_E_PARA`。
- `num>1`时（variable数组通路），`v_arr`须指向物理连续的variable数组，必须通过`ccu::array<variable>`申请。单独声明多个`variable`对象不保证物理连续，违反时在`store(...)`调用处即返回`CCU_E_PARA`。
- `addr_var`（variable地址通路）在调用本接口前须已被赋予有效地址值（通过`load_arg`、立即数赋值或算术运算）。
- 立即数地址通路在注册阶段确定，执行阶段不可变；需运行期动态地址时使用variable地址通路。

## 调用示例

```cpp
// 为简化示例，省略返回值接收与错误处理
using namespace AscendC::ccu;

// 场景1：将1个variable存储到立即数地址（单个variable -> 立即数地址）
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable result;
    // ... 对result进行计算 ...
    store(0x20000000ULL, result);
    return CCU_SUCCESS;
}

// 场景2：将4个variable批量存储到立即数地址（variable数组 -> 立即数地址）
CcuResult my_kernel2(ccu_kernel_arg arg) {
    array<variable> v_arr(4);    // 4个物理连续variable
    // ... 对v_arr进行计算 ...
    store(0x80000000ULL, v_arr, 4);
    return CCU_SUCCESS;
}

// 场景3：将variable存储到由variable指定的地址（单个variable -> variable地址）
CcuResult my_kernel3(ccu_kernel_arg arg) {
    variable dst_addr, result;
    load_arg(dst_addr, 0);    // host传入目标地址
    // ... 对result进行计算 ...
    store(dst_addr, result);  // 运行期间接寻址写入
    return CCU_SUCCESS;
}
```
