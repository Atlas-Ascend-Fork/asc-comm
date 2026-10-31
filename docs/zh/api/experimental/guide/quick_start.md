# 快速开始

## 环境准备

运行CCU程序需要：

1. 已安装的CCU AOT SDK，能够找到`ccuc`、`ccu_runtime.h`、`ccu_intrinsics.h`和
   `libccu_runtime.so.1`。
2. 与目标设备匹配的CANN/HCOMM public部署包。
3. 可用的设备、通信域和user stream。

CCU SDK不负责创建通信域。应用先按CANN/HCOMM部署环境创建communicator，
再把它传给CCU runtime。

## 第一个kernel

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
3. 用`ccu_context_create`创建context。
4. 准备设备输入/输出buffer，并取得token。
5. 查询部署环境提供的instance，并调用`ccu_context_bind_instance`绑定。
6. 构造`HcomCcuSchd`，通过chevron提交kernel。
7. 调用`ccu_synchronize`，检查结果。
8. 逆序释放context、设备内存、stream和通信域。

可直接参考
[`allgather_builtin`](../../../../../examples/ccu/ccu_builtin/01_allgather/README.md) 获取完整的
错误处理和清理代码。

## 编译

`copy_kernel.cc`由`ccuc`编译并生成Host launch wrapper；`copy_host.cc`使用普通Host编译器编译：

```bash
ccuc --ccu-arch=a5 -std=c++17 \
  -I"${ASCEND_HOME_PATH}/include" \
  -I"${ASCEND_HOME_PATH}/include/hcomm" \
  -I"${ASCEND_HOME_PATH}/include/hccl" \
  -I"${ASCEND_HOME_PATH}/pkg_inc" \
  -c copy_kernel.cc -o copy_kernel.o
g++ -std=c++17 \
  -I"${CCUC_HOME}/include" \
  -I"${ASCEND_HOME_PATH}/include" \
  -I"${ASCEND_HOME_PATH}/include/hcomm" \
  -I"${ASCEND_HOME_PATH}/include/hccl" \
  -I"${ASCEND_HOME_PATH}/pkg_inc" \
  -c copy_host.cc -o copy_host.o
g++ copy_host.o copy_kernel.o \
  -L"${CCUC_RUNTIME_DIR}" \
  -L"${ASCEND_HOME_PATH}/lib64" \
  -lccu_runtime -lhcomm -lascendcl -lc_sec -pthread \
  -Wl,-rpath,"${CCUC_RUNTIME_DIR}" \
  -Wl,-rpath,"${ASCEND_HOME_PATH}/lib64" \
  -o copy
```

## 运行前检查

- `rankSize`与kernel specialization一致。
- instance、channel、buffer和token来自同一个通信域/部署上下文。
- stream在launch、synchronize和cleanup完成前保持有效。
- 所有同步都使用有限超时。
