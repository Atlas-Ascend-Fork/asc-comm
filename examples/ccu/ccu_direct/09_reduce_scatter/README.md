# CCU ReduceScatter 直接执行样例

## 概述

本样例展示如何基于 HCCL 通信域和 CCU 数据面接口实现 ReduceScatter SUM。
Kernel 通过 ASC `<<<>>>` 直调方式下发，不再使用注册和 `asccomm_ccu_kernel_launch` 接口。

每个 rank 的输入按目标 rank 分为 `rankSize` 段。所有 rank 的同位置数据完成求和后，rank `r` 仅保留第 `r` 段：

```text
rank r recvBuf = SUM(所有 rank 的 segment_r)
```

CCU kernel 先使用 `GroupCopy` 将本 rank 对应的输入段复制到本地输出，再使用 `ccu::write_reduce` 将其他输入段归约到对应 rank 的输出。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## 目录结构介绍

```text
09_reduce_scatter/
├── CMakeLists.txt
├── README.md
├── README_en.md
├── run.sh
└── main.asc
```

## 样例描述

本样例基于HCCL通信域申请CCU资源，并通过ASC `<<<>>>` 直调CCU Kernel完成数据通信或搬运。

## 编译运行

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

## 执行结果

测试使用 `recvCount = rankSize`，rank `d` 的输入元素 `i` 初始化为 `d * 100 + i`。两卡场景下：

```text
rankId: 0, input: [ 0 1 2 3 ]
rankId: 1, input: [ 100 101 102 103 ]
rankId: 0, output: [ 100 102 ]
rankId: 0 PASS
rankId: 1, output: [ 104 106 ]
rankId: 1 PASS
```

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
