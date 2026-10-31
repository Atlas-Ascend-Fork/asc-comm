# 集合通信样例

## 概述

集合通信样例的控制面和数据面分离：Host准备通信域、instance、channel、buffer和token；
CCU kernel通过逻辑channel和地址完成实际搬运。以AllGather为例，每个rank提供一个输入
slice，完成后每个rank都得到所有rank的slice拼接结果。

## Host控制面

Host侧需要：

1. 根据`rank`和`rankSize`初始化通信域。
2. 查询通信域拥有的CCU instance。
3. 准备输入和输出设备buffer，并取得address/token。
4. 将`rankId`、`rankSize`和mission信息写入`HcomCcuSchd`。
5. 将输入地址、输出地址、token、slice大小和offset作为task参数传入kernel。
6. launch后调用`ccu_synchronize`。
7. 将输出复制回Host并按rank顺序校验。

AllGather的rank specialization由kernel声明，Host只需传入当前world size：

```cpp
HcomCcuSchd schd = {};
schd.missionNum = 1;
schd.missionStart = 0;
schd.rankId = rank;
schd.rankSize = world;

status = ccu_context_bind_instance(context, &instance);
status = allgather_kernel<<<schd, instance, stream>>>(
    input.address, output.address, input.token, slice_bytes);
if (status == CCU_STATUS_SUCCESS)
  status = ccu_synchronize(context, stream, 30000);
```

## CCU数据面

kernel使用编译器提供的channel资源。peer的顺序通常为除本rank外的rank，地址通过
channel关联的变量资源交换：

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

实际AllGather kernel还会根据rank specialization向peer写入对应slice；上述代码只展示
接口关系，不替代完整集合通信算法。

## 校验和资源释放

输出数组按以下顺序排列：

```text
[rank 0 slice | rank 1 slice | ... | rank (rankSize - 1) slice]
```

读取输出前必须完成同步。成功和失败路径都应按逆序释放：

```text
ccu_context_destroy
    -> device buffer
    -> stream
    -> communicator
    -> aclFinalize
```

如果同步超时或recovery失败，不要读取未完成的输出，也不要复用已经poisoned的context。

## 参考样例

- [`allgather_direct`](../../../../../examples/ccu/ccu_direct/01_allgather/README.md)
- [`allgather_direct kernel`](../../../../../examples/ccu/ccu_direct/01_allgather/main.asc)
- [`all_to_all_direct`](../../../../../examples/ccu/ccu_direct/05_all_to_all/README.md)

集合通信样例支持的rank、count和运行约束，以
[`examples/ccu`](../../../../../examples/ccu/README.md) 和对应样例README为准。
