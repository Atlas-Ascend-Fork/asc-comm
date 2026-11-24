# 快速开始

## 环境准备

运行CCU程序需要：

1. 安装与目标设备匹配的CANN包，并加载CANN环境。
2. 确认`ccuc`命令可用，且CCU编译所需的头文件和运行时库已随CANN环境安装。
3. 准备可用的设备和通信域。多rank样例需要对应数量的可见设备。

## 第一个kernel

以下片段声明kernel仅支持`rankSize`为2的通信域，并将输入拷贝到输出。完整的Host控制面
和运行脚本见下方单文件样例。

```cpp
__ccu_rank_sizes__(2)
__global__ __ccu__ void copy_kernel(__ccu_var_t input_address,
                                    __ccu_var_t output_address,
                                    __ccu_token_t token,
                                    __ccu_var_t bytes) {
  __ccu_event_t done;
  __ccu_addr_t input = __ccu_addr(input_address, token);
  __ccu_addr_t output = __ccu_addr(output_address, token);
  __ccu_local_copy_nb(output, input, bytes, done);
  __ccu_event_wait(done);
}
```

Host侧需要完成以下步骤：

1. 初始化设备和通信域。
2. 创建stream。
3. 准备设备输入/输出Buffer，并从HCOMM接口获取token。
4. 构造`HcomCcuSchd`，使用`<<<schd, instance, stream>>>`语法提交kernel。
5. 调用`ccu_synchronize`设置有限超时时间，检查结果。
6. 逆序释放设备内存、stream和通信域。

## 编译和运行

将Host代码和上述kernel写在同一个`copy.cc`中，并在Host代码里完成通信域、内存、
instance和stream的准备，以及kernel启动与结果检查。上面的kernel片段不能单独运行。
使用`ccuc`编译并链接该文件：

```bash
source /实际安装路径/cann/set_env.sh
ccuc -std=c++17 copy.cc -o copy
```

生成的`copy`按Host代码定义的参数启动；多rank程序须为每个rank准备对应进程和设备。
编译命令无需另行指定CCU头文件目录或手写CCU运行时链接参数。分文件编译见
[编译接口](../api/compiler.md)。

## 运行前检查

- `rankSize`与kernel通过[`__ccu_rank_sizes__`](../api/compiler.md#声明ranksize特例化)声明的特例化kernel一致。
- instance、channel、buffer和token来自同一个通信域。
- stream在launch、synchronize和cleanup完成前保持有效。
- 所有同步都使用有限超时。
