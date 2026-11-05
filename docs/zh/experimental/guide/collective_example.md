# 集合通信样例

## 概述

集合通信样例的控制面和数据面分离：Host准备通信域、instance、channel、buffer和token；
CCU kernel通过逻辑channel和地址完成实际搬运。以AllGather为例，每个rank提供一个输入
slice，完成后每个rank都得到所有rank的slice拼接结果。

## Host控制面

Host侧需要：

1. 根据`rank`和`rankSize`初始化通信域。
2. 从HCOMM接口获取通信域绑定的CCU instance。
3. 准备输入和输出设备Buffer，并从HCOMM接口获取address/token。
4. 将`rankId`、`rankSize`和任务信息写入`HcomCcuSchd`。
5. 将输入地址、输出地址、token和slice大小作为任务参数传入kernel。
6. 提交kernel后调用`ccu_synchronize`。
7. 将输出从设备内存复制回Host并按rank顺序校验。

AllGather的rank特例化由[`__ccu_rank_sizes__`](../api/compiler.md#声明ranksize特例化)声明，Host只需传入当前通信域的rankSize。示例中，
`input`和`output`为Host侧准备好的`ccu_buffer_view_t`，`slice_bytes`表示每个rank
输入数据的字节数：

```cpp
HcomCcuSchd schd = {};
schd.missionNum = 1;
schd.missionStart = 0;
schd.rankId = rank;
schd.rankSize = rankSize;

status = allgather_kernel<<<schd, instance, stream>>>(
    input.address, output.address, input.token, slice_bytes);
if (status == CCU_STATUS_SUCCESS)
  status = ccu_synchronize(instance, stream, 30000);
```

## 接口关系示例

kernel使用编译器提供的channel资源。`__ccu_channels[index]`表示一个对端，
`__ccu_channel_xns[index]`保存该对端交换的地址和token。

以下示例展示本地拷贝的接口关系：

```cpp
__ccu_rank_sizes__(2, 4, 8)
__global__ __ccu__ void allgather_kernel(
    __ccu_var_t input_address,
    __ccu_var_t output_address,
    __ccu_token_t token,
    __ccu_var_t slice_bytes) {
  __ccu_addr_t local_input = __ccu_addr(input_address, token);
  __ccu_addr_t local_output = __ccu_addr(output_address, token);
  __ccu_event_t done;

  __ccu_local_copy_nb(local_output, local_input, slice_bytes, done, 1);
  __ccu_event_wait(done, 1);
}
```

## 输出布局

输出Buffer按rank顺序拼接：

```text
[rank 0 slice | rank 1 slice | ... | rank (rankSize - 1) slice]
```

## 校验

调用`ccu_synchronize`成功后，从设备内存读取输出，并检查每个rank的slice位置和数值。

## 资源释放

成功和失败路径都应按创建的逆序释放资源：先释放设备Buffer，再释放stream，最后销毁通信域。

如果同步超时或恢复失败，不要读取未完成的输出，也不要复用已终止的instance。
