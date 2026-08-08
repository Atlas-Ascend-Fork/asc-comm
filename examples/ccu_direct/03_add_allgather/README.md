# CCU Direct Add + AllGather 样例

## 样例介绍

本样例展示如何先通过 AICore vector kernel 执行 AICore Add 计算，再以计算结果作为输入以直调第一阶段方式执行 CCU AllGather 通信。


## 目录结构

```text
03_add_allgather/
├── CMakeLists.txt
├── README.md
├── inc/
│   ├── binary_stream.h
│   ├── common.h
│   └── log.h
├── op_kernel_ccu/
│   ├── CMakeLists.txt
│   ├── ccu_kernel.cc
│   └── ccu_kernel.h
└── testcase/
    └── main.asc
```

## 构建和运行

执行前先设置 CANN 运行环境和 CCU 调度模式：

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
```

然后在样例目录执行：

```bash
mkdir -p build
cd build
cmake ..
make
./demo
```

## 结果示例

测试程序中，rank `d` 的输入元素初始化为 `d + 1`。两卡场景下，每个 rank 先本地加 1，再执行 AllGather，最终输出类似：

```text
Found 2 NPU device(s) available
rankId: 0, input: [ 1 1 1 ... ]
rankId: 1, input: [ 2 2 2 ... ]
rankId: 0, recvBuf: [ 2 2 2 ... 3 3 3 ... ]
rankId: 1, recvBuf: [ 2 2 2 ... 3 3 3 ... ]
```
