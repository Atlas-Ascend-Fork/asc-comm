# CCU Direct AllGather 样例

## 样例介绍

本样例展示如何基于 HCCL 通信域和 CCU 数据面接口，并以直调第一阶段方式实现 AllGather 集合通信操作。

每个 rank 的输入为一段 `sendCount` 个 FP32 元素。AllGather 完成后，每个 rank 的 `recvBuf` 按 rank 顺序保存所有 rank 的输入：

```text
rank r sendBuf = segment_r
rank p recvBuf = [segment_0 | segment_1 | ... | segment_N-1]
```


## 目录结构

```text
01_allgather/
├── CMakeLists.txt
├── README.md
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

测试程序中，rank `d` 的输入元素初始化为 `d + 1`。两卡场景下，运行成功后终端会输出类似以下信息：

```text
Found 2 NPU device(s) available
rankId: 0, input: [ 1 1 1 ... ]
rankId: 1, input: [ 2 2 2 ... ]
rankId: 0, recvBuf: [ 1 1 1 ... 2 2 2 ... ]
rankId: 1, recvBuf: [ 1 1 1 ... 2 2 2 ... ]
```
