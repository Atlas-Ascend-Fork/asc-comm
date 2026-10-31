# 编译与运行

## 源文件组织

建议每个样例至少包含两个文件：

| 文件 | 内容 |
| --- | --- |
| `*_kernel.cc` | CCU device kernel、intrinsic和生成Host wrapper |
| `*_host.cc` | 设备初始化、通信域、context、内存、launch、同步和结果校验 |

控制面代码不应把channel、资源编号或instruction ID写死。数据面只通过编译器提供的
逻辑channel、地址和token intrinsic访问资源。

## 推荐流程

### 编译kernel

```bash
ccuc --ccu-arch=a5 -std=c++17 \
  -I"${ASCEND_HOME_PATH}/include" \
  -I"${ASCEND_HOME_PATH}/include/hcomm" \
  -I"${ASCEND_HOME_PATH}/include/hccl" \
  -I"${ASCEND_HOME_PATH}/pkg_inc" \
  -c example_kernel.cc -o build/example_kernel.o
```

### 编译Host

```bash
g++ -std=c++17 -O2 -Wall -g \
  -I"${CCUC_HOME}/include" \
  -I"${ASCEND_HOME_PATH}/include" \
  -I"${ASCEND_HOME_PATH}/include/hcomm" \
  -I"${ASCEND_HOME_PATH}/include/hccl" \
  -I"${ASCEND_HOME_PATH}/pkg_inc" \
  -c example_host.cc -o build/example_host.o
```

### 链接

```bash
g++ build/example_host.o build/example_kernel.o \
  -L"${CCUC_RUNTIME_DIR}" -L"${ASCEND_HOME_PATH}/lib64" \
  -lccu_runtime -lhcomm -lascendcl -lc_sec -pthread \
  -Wl,-rpath,"${CCUC_RUNTIME_DIR}" \
  -Wl,-rpath,"${ASCEND_HOME_PATH}/lib64" -o build/example
```

HCOMM public library和平台runtime的路径由部署环境提供。不要把开发机绝对路径写进Makefile
或源文件。

### 运行

多rank程序由部署环境启动，每个进程接收自己的rank、设备和通信域信息。应用内部应保证
所有rank按相同顺序创建通信域、准备资源、提交对应collective并执行同步。

完整的编译和运行入口参见
[`allgather_direct`示例](../../../../../examples/ccu/ccu_direct/01_allgather/README.md)。
该示例需要在NPU环境中运行；运行前先按部署环境设置CANN环境，并开启CCU调度模式。

## 结果和失败处理

- launch返回成功只表示异步提交成功。
- 只有`ccu_synchronize`成功返回后，才可以读取设备输出。
- 返回`CCU_STATUS_TIMEOUT_RECOVERED`时，当前任务已经结束但发生过超时；不要在同一
  mission上继续追加任务。
- 返回`CCU_STATUS_RECOVERY_FAILED`或`CCU_STATUS_CONTEXT_POISONED`时，停止使用当前
  context，进入cleanup。
- 失败路径也必须销毁context、stream、设备内存和通信域。
