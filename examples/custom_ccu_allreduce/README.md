# CCU AllReduce 直接执行样例

## 样例介绍

本样例展示如何基于 HCCL 通信域和 CCU 数据面接口实现一个自定义 AllReduce SUM 操作。

样例约束：

- 样例固定使用 `HCCL_DATA_TYPE_FP32`
- 样例固定使用 `HCCL_REDUCE_SUM`
- 样例固定使用不同的 `sendBuf` 和 `recvBuf`
- 单机多卡 mesh 通信，每个 rank 最终得到相同的逐元素求和结果
- 使用 `HCCL_OP_EXPANSION_MODE=CCU_SCHED` 运行

当前版本用于对比定位 `GroupCopy + WriteReduce` 在 AllReduce 与 ReduceScatter
中的行为差异，其结构尽量与 `custom_ccu_reduce_scatter` 保持一致：

1. 每个 rank 获取本地 input 和 output 的 CCU token。
2. rank 间交换 output 地址和 output token，并等待所有远端参数到齐。
3. 使用 `GroupCopy` 将本地完整 input 复制到本地 output，作为本地贡献。
4. 等待 GroupCopy 和所有 rank 的初始化同步完成。
5. 使用 `ccu::WriteReduce` 将本地完整 input 累加到每个远端 output。
6. 所有远端写完成后执行后置同步。

它与 ReduceScatter 的核心区别是源地址：ReduceScatter 针对目标 rank 选择
`input + rankIdx * rankSliceSize`，而 AllReduce 始终使用完整 input 的起始地址。
该版本此前在两卡测试中出现过只保留本地 GroupCopy 结果的现象，用于继续分析
LoopGroup 与后续 WriteReduce 的生成及执行顺序。

## 目录结构

```text
custom_ccu_allreduce/
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

`run.sh` 会执行 CMake 配置、编译并运行：

```bash
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DASCEND_HOME_PATH="${ASCEND_HOME_PATH}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"
exec "${BUILD_DIR}/custom_allreduce_ccu"
```

## 结果示例

测试程序中，rank `r` 的第 `i` 个输入初始化为 `r * sendCount + i`。
两卡场景下输入和输出为：

```text
rankId: 0, input: [ 0 1 ]
rankId: 1, input: [ 2 3 ]
rankId: 0, output: [ 2 4 ]
rankId: 1, output: [ 2 4 ]
```

8 卡场景下，每个 rank 的输出都是：

```text
Found 8 NPU device(s) available
rankId: 0, output: [ 224 232 240 248 256 264 272 280 ]
rankId: 1, output: [ 224 232 240 248 256 264 272 280 ]
rankId: 2, output: [ 224 232 240 248 256 264 272 280 ]
rankId: 3, output: [ 224 232 240 248 256 264 272 280 ]
rankId: 4, output: [ 224 232 240 248 256 264 272 280 ]
rankId: 5, output: [ 224 232 240 248 256 264 272 280 ]
rankId: 6, output: [ 224 232 240 248 256 264 272 280 ]
rankId: 7, output: [ 224 232 240 248 256 264 272 280 ]
```
