# 快速开始

## 环境准备

运行CCU程序需要：

1. 已安装的开发环境，能够找到`ccuc`、`ccu_runtime.h`、`ccu_intrinsics.h`和
   `libccu_runtime.so`。
2. 与目标设备匹配的CANN/HCOMM部署包。
3. 可用的设备、通信域和stream。

## 第一个kernel

以下示例声明kernel仅支持`rankSize`为2的通信域。示例使用变量、令牌、地址和事件资源，
分别见[变量](../api/resource_allocation_operation/variable.md)、[令牌](../api/host/instance.md)、[地址](../api/resource_allocation_operation/address.md)和[事件](../api/resource_allocation_operation/event.md)。

```cpp
__ccu_rank_sizes__(2)
__global__ __ccu__ void copy_kernel(__ccu_var_t input,
                                    __ccu_var_t output,
                                    __ccu_token_t token,
                                    __ccu_var_t bytes) {
  __ccu_event_t done;
  __ccu_local_copy_nb(__ccu_addr(output, token),
                      __ccu_addr(input, token), bytes, done, 1);
  __ccu_event_wait(done, 1);
}
```

Host侧需要完成以下步骤：

1. 初始化设备和通信域。
2. 创建stream。
3. 准备设备输入/输出Buffer，并从HCOMM接口获取token。
4. 构造`HcomCcuSchd`，使用`<<<schd, instance, stream>>>`语法提交kernel。
5. 调用`ccu_synchronize`设置有限超时时间，检查结果。
6. 逆序释放设备内存、stream和通信域。

## 编译

以`copy_kernel.cc`和`copy_host.cc`为例，kernel源文件由`ccuc`编译，Host源文件使用
g++编译，最后链接为可执行程序。路径按实际开发环境调整：

```bash
ccuc -std=c++17 \
  -I"${CCUC_HOME}/include" \
  -I"${ASCEND_HOME_PATH}/include" \
  -I"${ASCEND_HOME_PATH}/include/ccu" \
  -I"${ASCEND_HOME_PATH}/include/hcomm" \
  -I"${ASCEND_HOME_PATH}/include/hcomm/ccu" \
  -I"${ASCEND_HOME_PATH}/include/hccl" \
  -c copy_kernel.cc -o copy_kernel.o
g++ -std=c++17 \
  -I"${CCUC_HOME}/include" \
  -I"${ASCEND_HOME_PATH}/include" \
  -I"${ASCEND_HOME_PATH}/include/ccu" \
  -I"${ASCEND_HOME_PATH}/include/hcomm" \
  -I"${ASCEND_HOME_PATH}/include/hcomm/ccu" \
  -I"${ASCEND_HOME_PATH}/include/hccl" \
  -c copy_host.cc -o copy_host.o
g++ copy_host.o copy_kernel.o \
  -L"${CCUC_RUNTIME_DIR}" \
  -L"${ASCEND_HOME_PATH}/lib64" \
  -lccu_runtime -lhcomm -lascendcl -lc_sec -pthread \
  -Wl,-rpath,"${CCUC_RUNTIME_DIR}" \
  -Wl,-rpath,"${ASCEND_HOME_PATH}/lib64" \
  -o copy
```

| 环境变量 | 说明 |
| --- | --- |
| `CCUC_HOME` | CCU开发环境目录，`include`目录位于该路径下 |
| `CCUC_RUNTIME_DIR` | `libccu_runtime.so`所在目录 |
| `ASCEND_HOME_PATH` | CANN安装目录 |

## 运行前检查

- `rankSize`与kernel通过[`__ccu_rank_sizes__`](../api/compiler.md#声明ranksize特例化)声明的特例化kernel一致。
- instance、channel、buffer和token来自同一个通信域。
- stream在launch、synchronize和cleanup完成前保持有效。
- 所有同步都使用有限超时。
