# CCU AllReduce 直接执行样例

## 概述

本样例展示如何基于 HCCL 通信域和 CCU 数据面接口实现一个自定义 AllReduce SUM 操作。
Kernel 通过 ASC `<<<>>>` 直调方式下发，不再使用注册和 `asccomm_ccu_kernel_launch` 接口。

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
4. 每个 rank 使用 `ccu::write_reduce(..., HCCL_REDUCE_SUM, ...)` 将本地 input 累加写入其它 rank 的 output。
5. 所有 rank 同步完成后，每个 rank 的 output 都是所有 rank 输入的逐元素和。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.1.0 |

## 目录结构介绍

```text
04_allreduce/
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

`run.sh` 会执行 CMake 配置、编译并运行：

```bash
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_ASC_ARCHITECTURES=dav-3510
cmake --build "${BUILD_DIR}" -j"$(nproc)"
exec "${BUILD_DIR}/ccu_allreduce"
```

## 执行结果

测试程序中，每个 rank 的输入数据都初始化为 `[0, 1, ..., devCount-1]`。因此两卡场景下，AllReduce SUM 后每个 rank 的输出都是：

```text
Found 2 NPU device(s) available
rankId: 0, output: [ 0 2 4 6 ]
rankId: 1, output: [ 0 2 4 6 ]
```

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
