# Hcomm AIV RoCE 批量 WriteNbi/ReadNbi 样例

## 概述

本样例使用 MPI 管理多进程，在每个 MPI rank 上选择一张 NPU，并通过 RoCE
`COMM_PROTOCOL_ROCE` 通道验证 `hcomm_aiv_roce.h` 提供的批量接口：

- `MakeBatchHandle`
- 入参为 `UbcBatchHandle` 的 `WriteNbi`
- 入参为 `UbcBatchHandle` 的 `ReadNbi`
- `BatchCommit`
- `Drain`

每个 rank 与环上的下一个 rank 建立通道。在同一个 batch handle 中连续调用 1000 次
`WriteNbi` 和 1000 次 `ReadNbi`；第 `i` 次调用使用 `i * 64` 字节的地址偏移。提交和
完成等待结束后，各 rank 校验前一个 rank 写入的 1000 个数据片段以及从后一个 rank 读回的
1000 个数据片段。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## 目录结构

```text
09_hcomm_roce_batch_write_read
├── CMakeLists.txt
├── roce_batch_write_read.asc // MPI、ACL、HCCL 通道和结果校验
├── roce_batch_write_read_kernel.cpp // AIV Kernel 批量接口调用
├── roce_batch_write_read_def.h    // Host/Kernel 共享常量和上下文
├── README.md
└── README_en.md
```

## 数据布局

每个 rank 注册一块通信内存，大小为 `3 * 1000 * 64` 字节：

| 区域 | 用途 |
| --- | --- |
| `[0, 64000)` | 本 rank 的源数据 |
| `[64000, 128000)` | 接收前一个 rank 的 `WriteNbi` 结果 |
| `[128000, 192000)` | 保存本 rank 的 `ReadNbi` 结果 |

批量 WQE 缓冲区位于 Kernel 的 VECOUT UB 中，大小为 `2000 * 64` 字节。

## 编译

先加载 CANN 环境，然后在本目录执行：

```bash
cd examples/aicore/hcomm/09_hcomm_roce_batch_write_read
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

编译本样例要求 MPI C++ 编译器（`mpicxx`）可用。

## 使能MPI环境变量

此处内容根据实际环境中MPI安装路径进行调整。

```bash
export MPI_HOME=/usr/local/mpi/mpich
export PATH="${MPI_HOME}/bin:${PATH}"
export LD_LIBRARY_PATH="${MPI_HOME}/lib:${LD_LIBRARY_PATH:-}"
```

## 运行

在单机多卡环境中，用 MPI 启动进程数个数的 rank，每个 rank 使用同编号的 NPU：

```bash
cd examples/aicore/hcomm/09_hcomm_roce_batch_write_read
source ${install_path}/cann/set_env.sh
mpirun -n 2 $(pwd)/build/hcomm_roce_batch_write_read
```

运行前需要确保 NPU 间的 RoCE 链路可用，MPI rank 数量不超过本机 NPU 数量，并为 MPI 进程
设置访问 NPU 所需的环境变量。运行成功时输出：

```text
[rank 0] hcomm_roce_batch_write_read | WriteNbi=1000 ReadNbi=1000 | PASS
[rank 1] hcomm_roce_batch_write_read | WriteNbi=1000 ReadNbi=1000 | PASS
RESULT | Example=hcomm_roce_batch_write_read | Status=PASS
```

## 注意事项

- 运行需要至少2张NPU，且rank数不超过当前可用NPU数量。
- 通道必须使用COMM_PROTOCOL_ROCE，与Kernel侧 `Hcomm<COMM_PROTOCOL_ROCE>` 保持一致。
- HcclGetRootInfo生成的root info由MPI Bcast分发给所有rank。
- 1000次WriteNbi和1000次ReadNbi使用同一个batch handle提交；Drain返回后才校验数据。
- 当rank数为2时，环上的前一个rank和后一个rank是同一个对端。
