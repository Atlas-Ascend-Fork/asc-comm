# CCU AllGather 直接执行样例

## 样例介绍

本样例展示如何基于 HCCL 通信编程接口和 CCU_SCHED 通信引擎实现 AllGather 集合通信操作。

样例不再构建自定义算子包，不安装 `opp/vendors/cust`，也不生成 `libhccl_custom_allgather.so`。样例源码会直接编译成可执行文件 `custom_allgather_ccu`，并依赖 hcomm+asc-comm 联合构建包中的动态库运行。

## 依赖关系

运行前需要先安装 hcomm+asc-comm 联合构建出的包，并确保安装目录中存在以下产物：

```text
${ASCEND_HOME_PATH}/lib64/libhcomm.so
${ASCEND_HOME_PATH}/lib64/libhccl.so
${ASCEND_HOME_PATH}/lib64/libasccomm_ccu_dataplane.so
${ASCEND_HOME_PATH}/include/ccu/ccu_types.h
```

样例 CMake 会同时加入以下 CCU 头文件搜索路径：

```text
${ASCEND_HOME_PATH}/include/ccu
${ASCEND_HOME_PATH}/include/ccu/hcomm
${ASCEND_HOME_PATH}/include/ccu/hcomm/ccu
```

其中 `ccu_variable.hpp`、`ccu_event.hpp` 等头文件内部会通过 `"ccu_primitives_impl.h"` 这类相对 include 引用实现头，因此不能只添加 `${ASCEND_HOME_PATH}/include`。

其中：

```text
libhcomm.so / libhccl.so:
  提供 HCCL 通信域、CCU 控制面和资源申请相关接口。

libasccomm_ccu_dataplane.so:
  提供 HcommCcuKernelRegister*、HcommCcuKernelLaunch、HcommCcuGetMemToken 等 CCU 数据面接口。
```

## 目录结构

```text
custom_ops_allgather/
├── CMakeLists.txt
├── README.md
├── run.sh
├── inc/
│   ├── binary_stream.h
│   ├── common.h
│   ├── hccl_custom_allgather.h
│   └── log.h
├── op_host/
│   ├── CMakeLists.txt
│   ├── allgather.cc
│   ├── utils.cc
│   └── utils.h
├── op_kernel_ccu/
│   ├── CMakeLists.txt
│   ├── ccu_kernel.cc
│   ├── ccu_kernel.h
│   ├── exec_op.cc
│   └── exec_op.h
└── testcase/
    └── main.cc
```

## 构建和运行

执行前先设置 CANN 运行环境和 CCU 调度模式：

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
```

然后在样例目录执行：

```bash
bash run.sh
```

`run.sh` 只负责执行 CMake 配置、编译和运行：

```bash
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DASCEND_HOME_PATH="${ASCEND_HOME_PATH}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"
exec "${BUILD_DIR}/custom_allgather_ccu"
```

## 结果示例

所有 rank 的输入数据初始化为该 rank 的 DeviceId。运行成功后，终端会输出类似以下信息：

```text
Found 2 NPU device(s) available
rankId: 0, input: [ 0 ]
rankId: 1, input: [ 1 ]
rankId: 0, output: [ 0 1 ]
rankId: 1, output: [ 0 1 ]
```

## 验证依赖

确认可执行文件不再依赖自定义算子包：

```bash
ldd build/custom_allgather_ccu | grep hccl_custom_allgather
```

预期无输出。

确认可执行文件依赖联合构建包：

```bash
ldd build/custom_allgather_ccu | grep -E 'hcomm|hccl|asccomm|ascendcl|acl'
```

预期能看到 `libhcomm.so`、`libhccl.so`、`libasccomm_ccu_dataplane.so` 等动态库。

## 常见问题

如果运行时报以下错误：

```text
RaTlvRequest fail
HcommCcuInsCreate failed
channel connect timeout
CreateChannels failed. engine[CCU]
```

说明程序已经进入 CCU 路径，但底层 CCU 控制通道、设备能力或 rank 间通信资源不可用，需要检查运行环境、设备能力和组网状态。

如果编译时报以下错误：

```text
fatal error: ccu_primitives_impl.h: No such file or directory
fatal error: ccu_launch.h: No such file or directory
```

说明 CCU 头文件子目录没有加入 include 搜索路径。需要确认 `CMakeLists.txt` 中包含 `${ASCEND_HOME_PATH}/include/ccu` 和 `${ASCEND_HOME_PATH}/include/ccu/hcomm/ccu`。
