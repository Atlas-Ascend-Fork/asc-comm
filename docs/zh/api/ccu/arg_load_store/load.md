# load

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

在CCU kernel内从片上内存地址读取一个或多个`uint64_t`值写入variable。

具体支持的数据通路为：

- 立即数地址 -> 单个variable
- 立即数地址 -> variable数组
- variable地址（间接寻址）-> 单个variable
- variable地址（间接寻址）-> variable数组

立即数地址在编译阶段确定，下发阶段不可变；variable地址在运行期从硬件寄存器读出。

## 函数原型

- 立即数地址 -> 单个variable

    ```cpp
    CcuResult load(uint64_t addr, variable v);
    ```

- 立即数地址 -> variable数组

    ```cpp
    CcuResult load(uint64_t addr, array<variable>& v_arr, uint32_t num);
    ```

- variable地址（间接寻址）-> 单个variable

    ```cpp
    CcuResult load(variable addr_var, variable v);
    ```

- variable地址（间接寻址）-> variable数组

    ```cpp
    CcuResult load(variable addr_var, array<variable>& v_arr, uint32_t num);
    ```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| addr | 输入 | 立即数片上内存地址（`uint64_t`，立即数地址通路）。须为CCU可访问的物理地址或经token化的VA，在编译阶段确定，下发阶段不可变。 |
| addr_var | 输入 | 地址variable（`variable`，variable地址通路）。运行期该variable中存储的值被用作片上内存地址，须已赋有效地址值。 |
| v | 输入/输出 | 目标variable（单个variable通路）。运行期从片上内存地址处读取8字节写入该variable。 |
| v_arr | 输入/输出 | 目标variable数组首元素（variable数组通路）。须通过`ccu::array<variable>`申请以保证物理连续。 |
| num | 输入 | 加载的`uint64_t`元素个数（variable数组通路）。须满足`1 ≤ num ≤ v_arr.size()`；`num>1`时依次从起始地址开始、按8字节步长读取，分别写入`v_arr[0]`至`v_arr[num-1]`。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PARA` | 参数错误：`num`为0或大于`v_arr.size()`；或`num>1`时`v_arr`元素物理不连续。 |
| `CCU_E_PTR` | 接口在编译阶段之外被调用。 |
| `CCU_E_NOT_FOUND` | 传入的`v`/`v_arr`句柄（或`v_arr[1..num-1]`的相邻句柄）未在当前kernel内创建。 |

## 约束说明

- `addr`须为CCU可访问的物理地址或经token化的VA，直接传入未经token化的进程虚拟地址将触发驱动错误。
- `num`须满足`1 ≤ num ≤ v_arr.size()`。本接口在访问`v_arr[0]`之前完成该校验，传入0或超过`v_arr.size()`时返回`CCU_E_PARA`，不会触达底层接口；空`array`（`v_arr.size()`为0）传入任意`num`同样返回`CCU_E_PARA`。
- `num>1`时（variable数组通路），`v_arr`须指向物理连续的variable数组，必须通过`ccu::array<variable>`申请。单独声明多个`variable`对象不保证物理连续，违反时在`load(...)`调用处即返回`CCU_E_PARA`。
- `addr_var`（variable地址通路）在调用本接口前须已被赋予有效地址值（通过`load_arg`、立即数赋值或算术运算）。
- 立即数地址通路在编译阶段确定，下发阶段不可变；需运行期动态地址时使用variable地址通路。

## 调用示例

```cpp
// 为简化示例，省略返回值接收与错误处理
using namespace AscendC::ccu;

// 场景1：从立即数地址加载1个variable（立即数地址 -> 单个variable）
CcuResult my_kernel(ccu_kernel_arg arg) {
    variable v;
    load(0x10000000ULL, v);
    return CCU_SUCCESS;
}

// 场景2：从立即数地址批量加载4个variable（立即数地址 -> variable数组）
CcuResult my_kernel2(ccu_kernel_arg arg) {
    array<variable> v_arr(4);    // 4个物理连续variable
    load(0x80000000ULL, v_arr, 4);
    return CCU_SUCCESS;
}

// 场景3：从variable地址间接加载（variable地址 -> 单个variable，地址由上一步计算得出）
CcuResult my_kernel3(ccu_kernel_arg arg) {
    variable src_addr, v;
    load_arg(src_addr, 0);    // host传入地址
    load(src_addr, v);        // 运行期间接寻址
    return CCU_SUCCESS;
}
```
