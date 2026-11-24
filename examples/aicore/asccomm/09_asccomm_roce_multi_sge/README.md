# Asccomm RoCE 多 SGE 读写样例

## 概述

本样例演示在 RoCE 通道上通过多 SGE 版本 `write_nbi` 和 `read_nbi` 连续提交离散内存段，并支持 Batch 版和普通版两种执行方式。用户通过 `--mode batch|normal` 选择执行方式，默认值为 batch。MPI 负责多进程管理、HCCL root info 交换和各阶段同步。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR&950DT系列产品 | >= CANN 9.3.0 |

## 样例行为

- Batch 模式下，`write_nbi` 调用 128 次。每次调用的 `srcDescs` 包含 8 个 64B 离散数据段，远端
  `dst` 是一个 512B 连续数据区。
- Batch 模式下，`read_nbi` 调用 128 次。每次调用的 `dstDescs` 包含 8 个 64B 离散数据段，远端
  `src` 是一个 512B 连续数据区。
- Batch 模式下，除最后一次 `write_nbi` 和最后一次 `read_nbi` 外，其余调用均通过 `cqe=0` 配置关闭
  完成事件生成；最后两次调用使用默认 config 模板参数，其 `cqe=1`。因此写侧和读侧各产生 1 个
  CQE，`drain` 内的 `poll_cq` 只需处理对应的完成事件。
- 普通版模式下，`write_nbi` 和 `read_nbi` 各只调用一次。请求通过 `commit=false` 延迟门铃发起，
  随后调用一次 `commit` 发布该 WQE，并执行一次 `drain`。
- Batch 模式下，128 次 `write_nbi` 或 `read_nbi` 全部加入同一个 batch handle，再执行一次
  `batch_commit` 和 `drain`。
- rank i 与 rank i + N/2 两两成对通信。每个 pair 中，低 rank 执行写请求，高 rank 校验落地数据；高 rank 再执行读请求并校验本端离散段内容。

## 目录结构

```text
09_asccomm_roce_multi_sge
├── CMakeLists.txt
├── README.md
├── README_en.md
├── roce_multi_sge.asc
├── roce_multi_sge_def.h
└── roce_multi_sge_kernel.cpp
```

## 数据布局

每个rank注册一块393216字节通信内存，分为四个区域：

| 偏移 | 大小 | 用途 |
| --- | ---: | --- |
| `[0, 131072)` | 131072B | 本地离散源数据，128次调用 × 8段，每段64B、步长128B |
| `[131072, 196608)` | 65536B | 连续源数据，128条512B消息；每个pair的低rank初始化，高rank通过 `read_nbi` 读取 |
| `[196608, 262144)` | 65536B | 每个pair的高rank接收低rank通过 `write_nbi` 写入的128条512B连续消息 |
| `[262144, 393216)` | 131072B | 保存 `read_nbi` 写回的本地离散结果，每段64B、步长128B |

批量WQE缓冲区位于Kernel的VECOUT UB，大小为 `128 * 3 * 64` 字节。batch版将其作为连续WQE staging buffer；普通版将其作为单条WQE构造区。

## 编译

本样例与 08 样例相同，直接使用 CANN 环境中已安装的 asc-comm 头文件；该安装包需要包含
RoCE 批量多 SGE 接口。

```bash
cd examples/aicore/asccomm/09_asccomm_roce_multi_sge
source ${install_path}/cann/set_env.sh
export MPI_HOME=/usr/local/mpich
mkdir build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

如果要使能 `ASCENDC_DEBUG`，请使用 `cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 -DASCENDC_DEBUG=ON ..`。

RoCE 后端通过 CMake 选项 `ASCCOMM_ROCE_BACKEND_MLX5` 选择：不开启该选项时，使用默认的 1825 网卡；开启该选项时，使用 mlx5 网卡。该选项默认开启，可通过 `-DASCCOMM_ROCE_BACKEND_MLX5=OFF` 或 `-DASCCOMM_ROCE_BACKEND_MLX5=ON` 显式指定。

## 使能MPI环境变量

此处内容根据实际环境中MPI安装路径进行调整。

```bash
export MPI_HOME=/usr/local/mpich
export PATH="${MPI_HOME}/bin:${PATH}"
export LD_LIBRARY_PATH="${MPI_HOME}/lib:${LD_LIBRARY_PATH:-}"
```

## 运行

```bash
cd examples/aicore/asccomm/09_asccomm_roce_multi_sge
source ${install_path}/cann/set_env.sh
mpirun -n 4 $(pwd)/build/asccomm_roce_multi_sge

使用普通版流程：

mpirun -n 4 $(pwd)/build/asccomm_roce_multi_sge --mode normal

显式选择批量流程：

mpirun -n 4 $(pwd)/build/asccomm_roce_multi_sge --mode batch
```

`-n 4` 表示启动4个MPI进程，即4个通信rank；每个进程默认使用同编号的NPU。4可按实际NPU数量替换，必须是大于等于2的偶数。

batch 模式运行成功时输出：

```text
[rank 1] asccomm_roce_multi_sge | peer=0 mode=batch write_nbi=128 SGEs=8 | PASS
[rank 1] asccomm_roce_multi_sge | peer=0 mode=batch read_nbi=128 SGEs=8 | PASS
[rank 3] asccomm_roce_multi_sge | peer=1 mode=batch write_nbi=128 SGEs=8 | PASS
[rank 3] asccomm_roce_multi_sge | peer=1 mode=batch read_nbi=128 SGEs=8 | PASS
RESULT | Example=asccomm_roce_multi_sge | Status=PASS
```

normal 模式运行成功时输出：

```text
[rank 1] asccomm_roce_multi_sge | peer=0 mode=normal write_nbi=1 SGEs=8 | PASS
[rank 1] asccomm_roce_multi_sge | peer=0 mode=normal read_nbi=1 SGEs=8 | PASS
[rank 3] asccomm_roce_multi_sge | peer=1 mode=normal write_nbi=1 SGEs=8 | PASS
[rank 3] asccomm_roce_multi_sge | peer=1 mode=normal read_nbi=1 SGEs=8 | PASS
RESULT | Example=asccomm_roce_multi_sge | Status=PASS
```

## 注意事项

- 运行需要至少2张NPU，rank数必须为偶数且不超过可用NPU数量。
- 每对 rank i 与 rank i + N/2 之间必须存在RoCE链路，且通道协议使用COMM_PROTOCOL_ROCE。
- CANN安装包必须包含RoCE批量多SGE接口。
- batch模式下，128次write_nbi或read_nbi调用使用同一个batch handle一次性提交，然后统一drain。
- 普通版模式下不创建batch handle；write_nbi或read_nbi只发起一次，随后commit并drain，校验第一条512B消息。
- 本地段地址和长度由srcDescs/dstDescs描述，所有段必须位于已注册的通信内存内。
