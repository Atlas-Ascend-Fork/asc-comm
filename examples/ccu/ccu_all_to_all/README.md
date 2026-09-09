# CCU AllToAll 直接执行样例

## 样例介绍

本样例展示如何基于 HCCL 通信域和 CCU 数据面接口实现等长 AllToAll。

每个 rank 的输入按目标 rank 分为 `rankSize` 段，并向每个目标 rank 发送 `perCount` 个元素。目标 rank 的输出按源 rank 顺序保存：

```text
rank r sendBuf = [to_rank_0 | to_rank_1 | ... | to_rank_N-1]
rank p recvBuf = [from_rank_0 | from_rank_1 | ... | from_rank_N-1]
```

CCU kernel 使用 `GroupCopy` 完成本地段复制，并使用 `ccu::Write` 将其他输入段写入远端输出。AllToAll 不执行归约。

样例约束：

- 仅支持 `HCCL_DATA_TYPE_FP32`
- 每个源到每个目标的元素数由 `perCount` 指定
- 输入和输出使用不同的 Device buffer，不支持原地操作
- 支持最多 16 个 rank 的单机 1D full mesh
- 使用 `HCCL_OP_EXPANSION_MODE=CCU_SCHED` 运行

## 目录结构

```text
ccu_all_to_all/
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

测试使用 `perCount = rankSize`，rank `d` 的输入元素 `i` 初始化为 `d * 100 + i`。两卡场景下：

```text
rankId: 0, input: [ 0 1 2 3 ]
rankId: 1, input: [ 100 101 102 103 ]
rankId: 0, output: [ 0 1 100 101 ]
rankId: 0 PASS
rankId: 1, output: [ 2 3 102 103 ]
rankId: 1 PASS
```
