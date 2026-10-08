# write

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

通过已建链的[`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/ChannelHandle.md)将本端数据异步写入对端片上内存，硬件完成时自动将`event`中`mask`对应的标志位设置为1。

具体支持的数据通路为：

- 本端片上内存 -> 对端片上内存
- 本端CCU Buffer -> 对端片上内存

## 函数原型

- 本端片上内存 -> 对端片上内存

    ```cpp
    CcuResult write(ChannelHandle ch, remote_addr remote, local_addr local,
                    variable len, event event, uint16_t mask = 1);
    ```

- 本端CCU Buffer -> 对端片上内存

    ```cpp
    CcuResult write(ChannelHandle ch, remote_addr remote, ccu_buffer local,
                    variable len, event event, uint16_t mask = 1);
    ```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| ch | 输入 | 跨rank通道句柄。 |
| remote | 输入 | 目的地址，类型为`remote_addr`。 |
| local | 输入 | 源地址，<br>&bull;本端片上内存 -> 对端片上内存：类型为`local_addr`。<br>&bull;本端CCU Buffer -> 对端片上内存：类型为`ccu_buffer`。 |
| len | 输入 | 写入字节数，类型为`variable`，实际值在执行阶段取自对应的通用寄存器。 |
| event | 输入 | 完成事件对象，硬件写入完成时自动将`event`中`mask`对应的标志位设置为1，下游调用`event_wait(event, mask)`等待。 |
| mask | 输入 | 16位事件掩码，指定硬件完成时设置`event`的哪些标志位。默认值为`1`，即只置位bit0。 |

## 返回值说明

[`CcuResult`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/CcuResult.md)：接口成功返回`CCU_SUCCESS`，其他值表示失败。

| 返回值 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 操作成功。 |
| `CCU_E_PTR` | 接口在注册阶段之外被调用。 |
| `CCU_E_NOT_FOUND` | 传入的`remote`/`local`/`len`/`event`句柄未在当前kernel注册。 |

## 约束说明

- 参数顺序为目的端在前、源端在后：`write(ch, remote, local, ...)`，顺序写反会因类型不同在编译期报错。
- 同一kernel内所有[`ChannelHandle`](https://gitcode.com/cann/hcomm/blob/master/docs/zh/api_ref/comm_opdev/datatype_definition/ChannelHandle.md)须属于同一die，不同die的channel不可混入同一kernel。die一致性由`<<<>>>`统一校验，不一致时返回`CCU_E_PARA`，而非本接口的返回值。
- “本端CCU Buffer -> 对端片上内存”时，`len`不可超过`ccu_buffer`单片大小（4096字节）；该上限须由调用方自行保证，超出时硬件会发生未定义行为。
- 本接口为异步操作，须通过`event_wait(event, mask)`等待写入完成，方可保证对端数据可见。

## 调用示例

```cpp
using namespace AscendC::ccu;

// 用户自定义的kernel入参结构体，Host侧建链后通过<<<>>>的kernelArg传入
struct my_kernel_arg {
    ChannelHandle channel_handle;
};

// 场景1：本端片上内存→对端片上内存
CcuResult my_kernel(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg为void*，先转型为用户入参结构体
    ChannelHandle ch = params->channel_handle;
    remote_addr remote;
    local_addr src;
    variable len;
    event evt;

    write(ch, remote, src, len, evt);    // remote在前，local在后
    event_wait(evt);
    return CCU_SUCCESS;
}

// 场景2：本端CCU Buffer→对端片上内存
CcuResult my_kernel2(ccu_kernel_arg arg) {
    auto *params = static_cast<my_kernel_arg *>(arg);  // ccu_kernel_arg为void*，先转型为用户入参结构体
    ChannelHandle ch = params->channel_handle;
    remote_addr remote;
    ccu_buffer buf;
    variable len;
    event evt;

    write(ch, remote, buf, len, evt);
    event_wait(evt);
    return CCU_SUCCESS;
}
```
