# local_reduce

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

将源数据与目标内存或多个ccu_buffer按指定算子合并（异步），硬件完成时自动将`evt`中`mask`对应的标志位设置为1。

具体支持的数据通路为：

- 本端片上内存 -> 本端片上内存（原地归约：`dst = reduce(dst, src, op_type)`，输入输出类型相同）
- N个本端CCU Buffer -> `buffers[0]`（2 ≤ N ≤ 8，硬件一次性归约N路；输出类型支持与输入同精度，或在`HCCL_REDUCE_SUM`下升精度输出，详见`output_data_type`参数说明）

## 函数原型

- 本端片上内存 -> 本端片上内存

    ```cpp
    CcuResult local_reduce(local_addr dst, local_addr src, variable len,
                           HcclDataType data_type, HcclReduceOp op_type,
                           event evt, uint16_t mask = 1);
    ```

- N个本端CCU Buffer -> `buffers[0]`（2 ≤ count ≤ 8）

    ```cpp
    CcuResult local_reduce(ccu_buffer* buffers, uint32_t count,
                           HcclDataType data_type, HcclDataType output_data_type,
                           HcclReduceOp op_type,
                           variable len, event evt, uint16_t mask = 1);
    ```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| dst | 输入/输出 | 目标片上内存地址（`local_addr`，本端片上内存 -> 本端片上内存）。调用前须写入有效初值；硬件完成后更新为归约结果。 |
| src | 输入 | 源片上内存地址（`local_addr`，本端片上内存 -> 本端片上内存）。 |
| buffers | 输入/输出 | ccu_buffer数组首地址（`ccu_buffer*`，N个本端CCU Buffer -> `buffers[0]`），不可为`nullptr`。推荐通过`ccu::array<ccu_buffer>`申请以保证物理连续。`buffers[0]`为归约结果的输出位置，调用前须写入有效初值。精度膨胀场景（低精度输入 + SUM 升精度输出，如INT8→FP32时输出元素4×于输入）下，传入的ccu_buffer数组须同时覆盖输入与膨胀后的输出占用（例如2路INT8输入升FP32输出时须预留4个ccu_buffer，而非2个）；预留不足会导致硬件读写越界、行为未定义。 |
| count | 输入 | ccu_buffer数量（N个本端CCU Buffer -> `buffers[0]`）。取值范围为`[2, 8]`（超过上限会抛出异常并携带错误码）。`count == 0`会被直接拒绝并返回`CCU_E_PARA`；`count == 1`不会被拒绝但硬件行为未定义，单个ccu_buffer场景请用“本端片上内存 -> 本端片上内存”通路。`count`须等于`buffers`数组实际长度。 |
| data_type | 输入 | 输入数据类型，取值见`HcclDataType`枚举。仅支持以下6种：`HCCL_DATA_TYPE_UINT8`、`HCCL_DATA_TYPE_INT16`、`HCCL_DATA_TYPE_INT32`、`HCCL_DATA_TYPE_FP16`、`HCCL_DATA_TYPE_FP32`、`HCCL_DATA_TYPE_BFP16`。其他取值会被拒绝并抛出异常（携带错误码）。 |
| output_data_type | 输入 | 输出数据类型（仅“N个本端CCU Buffer -> `buffers[0]`”通路），取值见`HcclDataType`枚举。支持两种组合：①同精度——取值与`data_type`相同；②升精度——仅在`op_type == HCCL_REDUCE_SUM`时支持，将低精度输入归约后升至高精度输出（如`HCCL_DATA_TYPE_INT8`→`HCCL_DATA_TYPE_FP32`，见“调用示例 - 场景2”）。其他`data_type`/`output_data_type`组合返回`CCU_E_NOT_SUPPORT`（见返回值表）。升精度场景下`buffers`须按精度膨胀比例预留ccu_buffer，详见`buffers`参数说明。 |
| op_type | 输入 | 归约算子，取值见`HcclReduceOp`枚举。仅支持`HCCL_REDUCE_SUM`、`HCCL_REDUCE_MAX`、`HCCL_REDUCE_MIN`；`HCCL_REDUCE_PROD`不支持，传入会被拒绝并抛出异常（携带错误码）。 |
| len | 输入 | 操作字节数，类型为`variable`，实际值在下发阶段取自对应的通用寄存器。<br>&bull;本端片上内存 -> 本端片上内存：操作的总字节数。<br>&bull;N个本端CCU Buffer -> `buffers[0]`：每个ccu_buffer参与归约的字节数，不可超过单片大小（4096字节）。 |
| evt | 输入 | 完成事件对象，硬件归约完成时自动将`evt`中`mask`对应的标志位设置为1，下游调用`event_wait(evt, mask)`等待。 |
| mask | 输入 | 16位事件掩码，指定硬件完成时设置`event`的哪些标志位。默认值为`1`，即只置位bit0。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PARA` | 参数错误，包括`buffers`为`nullptr`或`count`为0。 |
| `CCU_E_NOT_SUPPORT` | 仅“N个本端CCU Buffer -> `buffers[0]`”通路：`data_type`/`output_data_type`组合不满足升精度/同精度约束（详见`output_data_type`说明）。 |

## 约束说明

- 归约为原地操作，调用前`dst`（“本端片上内存 -> 本端片上内存”通路）或`buffers[0]`（“N个本端CCU Buffer -> `buffers[0]`”通路）须已写入有效初值，否则归约结果未定义。
- “N个本端CCU Buffer -> `buffers[0]`”通路的`buffers`须指向物理连续的`ccu_buffer`数组，推荐使用`ccu::array<ccu_buffer>`申请。
- “N个本端CCU Buffer -> `buffers[0]`”通路的`count`取值范围为`[2, 8]`；`count > 8`时抛出异常（携带错误码），`count == 0`直接返回`CCU_E_PARA`，`count == 1`不被拒但硬件行为未定义（请改用“本端片上内存 -> 本端片上内存”通路）。
- “N个本端CCU Buffer -> `buffers[0]`”通路在精度膨胀场景（低精度输入 + SUM 升精度输出）下，`buffers`须按膨胀比例额外预留ccu_buffer以容纳膨胀后的输出，否则硬件读写越界、行为未定义。
- “N个本端CCU Buffer -> `buffers[0]`”通路，`len`不可超过`ccu_buffer`单片大小（4096字节）；该上限须由调用方自行保证，超出会导致运行期硬件行为未定义。
- 本接口为异步操作，须通过`event_wait(evt, mask)`等待归约完成后再读取结果。

## 调用示例

```cpp
// 为简化示例，省略返回值接收与错误处理
using namespace AscendC::ccu;

// 场景1：本端片上内存 -> 本端片上内存归约（FP16 SUM）
CcuResult my_kernel(ccu_kernel_arg arg) {
    local_addr dst, src;
    variable len;
    event evt;

    // dst须预先写入初值（如0.0）
    local_reduce(dst, src, len, HCCL_DATA_TYPE_FP16, HCCL_REDUCE_SUM, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}

// 场景2：2个ccu_buffer归约到buffers[0]，输入INT8升精度输出FP32（合法的“输入≠输出”组合；
//        INT8→FP32 = 4× 膨胀，2路输入须预留4个ccu_buffer以容纳膨胀后的输出，见参数表`buffers`说明）
CcuResult my_kernel2(ccu_kernel_arg arg) {
    array<ccu_buffer> bufs(4);    // 2路输入，按膨胀比例预留4个物理连续ccu_buffer
    variable len;
    event evt;

    // bufs[0]和bufs[1]为2路INT8输入（须预先写入初值）；
    // 归约结果为FP32，从buffers[0]起写入，膨胀后占用4个ccu_buffer
    local_reduce(bufs.data(), 2,
                 HCCL_DATA_TYPE_INT8, HCCL_DATA_TYPE_FP32,
                 HCCL_REDUCE_SUM, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}

// 场景3：输入输出同类型的常规归约（FP16 SUM）
CcuResult my_kernel3(ccu_kernel_arg arg) {
    array<ccu_buffer> bufs(4);
    variable len;
    event evt;

    local_reduce(bufs.data(), 4,
                 HCCL_DATA_TYPE_FP16, HCCL_DATA_TYPE_FP16,
                 HCCL_REDUCE_SUM, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
