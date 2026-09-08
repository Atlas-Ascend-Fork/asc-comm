# CCU ReduceScatter 直接执行样例

## 样例介绍

本样例展示如何基于 HCCL 通信域和 CCU 数据面接口实现 ReduceScatter SUM。

每个 rank 的输入按目标 rank 分为 `rankSize` 段。所有 rank 的同位置数据完成求和后，rank `r` 仅保留第 `r` 段：

```text
rank r recvBuf = SUM(所有 rank 的 segment_r)
```

CCU kernel 先使用 `GroupCopy` 将本 rank 对应的输入段复制到本地输出，再使用 `ccu::WriteReduce` 将其他输入段归约到对应 rank 的输出。


## 目录结构

```text
ccu_reduce_scatter/
├── CMakeLists.txt
├── README.md
├── run.sh
├── main.cc
├── op_host/
│   ├── CMakeLists.txt
│   ├── alg_resource.cc
│   ├── alg_resource.h
│   ├── exec_op.cc
│   └── exec_op.h
└── op_kernel_ccu/
    ├── CMakeLists.txt
    ├── ccu_kernel.cc
    └── ccu_kernel.h
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

`run.sh` 会执行 CMake 配置、编译并运行样例。

## 结果示例

测试使用 `recvCount = rankSize`，rank `d` 的输入元素 `i` 初始化为 `d * 100 + i`。两卡场景下：

```text
rankId: 0, input: [ 0 1 2 3 ]
rankId: 1, input: [ 100 101 102 103 ]
rankId: 0, output: [ 100 102 ]
rankId: 0 PASS
rankId: 1, output: [ 104 106 ]
rankId: 1 PASS
```
