# CCU Direct AllGather + Add 样例

## 样例介绍

本样例展示如何先以直调第一阶段方式通过 CCU 完成 AllGather 通信，再通过 AICore vector kernel 对 AllGather 结果执行 AICore Add 计算。


## 目录结构

```text
02_allgather_add/
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

测试程序中，rank `d` 的输入元素初始化为 `d + 1`。两卡场景下，AllGather 后的数据为 `[1 ... 2 ...]`，再经过 AICore 加 1，最终输出类似：

```text
Found 2 NPU device(s) available
rankId: 0, input: [ 1 1 1 ... ]
rankId: 1, input: [ 2 2 2 ... ]
rankId: 0, computeBuf: [ 9 9 9 ... 10 10 10 ... ]
rankId: 1, computeBuf: [ 9 9 9 ... 10 10 10 ... ]
```
