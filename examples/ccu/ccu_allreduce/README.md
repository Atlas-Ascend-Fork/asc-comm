# CCU AllReduce 直接执行样例

## 样例介绍

本样例展示如何基于 HCCL 通信域和 CCU 数据面接口实现一个自定义 AllReduce SUM 操作。

样例约束：

- 样例固定使用 `HCCL_DATA_TYPE_FP32`
- 样例固定使用 `HCCL_REDUCE_SUM`
- 样例固定使用不同的 `sendBuf` 和 `recvBuf`
- 单机多卡 mesh 通信，每个 rank 最终得到相同的逐元素求和结果
- 使用 `HCCL_OP_EXPANSION_MODE=CCU_SCHED` 运行

算法流程：

1. 每个 rank 按 allgather 样例的方式获取一个 CCU token。
2. 每个 rank 将本地 input 复制到本地 output，作为本 rank 对结果的初始贡献。
3. 所有 rank 同步，确保每个 output 都已经初始化。
4. 每个 rank 使用 `ccu::WriteReduce(..., HCCL_REDUCE_SUM, ...)` 将本地 input 累加写入其它 rank 的 output。
5. 所有 rank 同步完成后，每个 rank 的 output 都是所有 rank 输入的逐元素和。

## 目录结构

```text
ccu_allreduce/
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
exec "${BUILD_DIR}/ccu_allreduce"
```

## 结果示例

测试程序中，每个 rank 的输入数据都初始化为 `[0, 1, ..., devCount-1]`。因此 8 卡场景下，AllReduce SUM 后每个 rank 的输出都是：

```text
Found 8 NPU device(s) available
rankId: 0, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 1, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 2, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 3, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 4, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 5, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 6, output: [ 0 8 16 24 32 40 48 56 ]
rankId: 7, output: [ 0 8 16 24 32 40 48 56 ]
```
