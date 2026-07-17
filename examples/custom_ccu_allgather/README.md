# CCU AllGather 直接执行样例

## 样例介绍

本样例展示如何基于CCU通信编程接口实现AllGather集合通信操作。

样例依赖：
```text
CANN包：
cann-hcomm_9.1.0_*.run
Ascend-cann-toolkit_9.1.0_*.run
其中：
libhcomm.so / libhccl.so:
  提供 HCCL 通信域、CCU 控制面和资源申请相关接口。

libasccomm_ccu_dataplane.so:
  提供 HcommCcuKernelRegister*、HcommCcuKernelLaunch、HcommCcuGetMemToken 等 CCU 数据面接口。
```

## 目录结构

```text
custom_ccu_allgather/
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