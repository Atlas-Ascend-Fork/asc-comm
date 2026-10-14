# Hcomm RoCE 多 SGE 批量读写样例

## 概述

本样例演示在 RoCE 通道上调用 `MakeBatchHandle` 创建 `UbcBatchHandle`，并用其多 SGE 版本
`WriteNbi`、`ReadNbi` 连续提交离散内存段。MPI 负责多进程管理、HCCL root info 交换和各阶段同步。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## 样例行为

- `WriteNbi` 调用 128 次。每次调用的 `srcDescs` 包含 8 个 64B 离散数据段，远端 `dst` 是一个
  512B 连续数据区。
- `ReadNbi` 调用 128 次。每次调用的 `dstDescs` 包含 8 个 64B 离散数据段，远端 `src` 是一个
  512B 连续数据区。
- 每 4 次 `WriteNbi` 执行一次 `BatchCommit` 和 `Drain`；128 次 `ReadNbi` 同样按 4 次一组
  提交和清排。
- rank 0 执行写请求，rank 1 校验落地数据；rank 1 再执行读请求并校验本端离散段内容。

## 目录结构

```text
10_hcomm_roce_multi_sge
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
| `[131072, 196608)` | 65536B | 连续源数据，128条512B消息；rank 0初始化，rank 1通过 `ReadNbi` 读取 |
| `[196608, 262144)` | 65536B | rank 1接收rank 0通过 `WriteNbi` 写入的128条512B连续消息 |
| `[262144, 393216)` | 131072B | 保存 `ReadNbi` 写回的本地离散结果，每段64B、步长128B |

批量WQE缓冲区位于Kernel的VECOUT UB，大小为 `4 * 8 * 64` 字节，即每4次调用复用一批WQE。

## 编译

本样例与 09 样例相同，直接使用 CANN 环境中已安装的 asc-comm 头文件；该安装包需要包含
RoCE 批量多 SGE 接口。

```bash
cd examples/aicore/hcomm/10_hcomm_roce_multi_sge
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

## 使能MPI环境变量

此处内容根据实际环境中MPI安装路径进行调整。

```bash
export MPI_HOME=/usr/local/mpi/mpich
export PATH="${MPI_HOME}/bin:${PATH}"
export LD_LIBRARY_PATH="${MPI_HOME}/lib:${LD_LIBRARY_PATH:-}"
```

## 运行

```bash
cd examples/aicore/hcomm/10_hcomm_roce_multi_sge
source ${install_path}/cann/set_env.sh
mpirun -n 2 $(pwd)/build/hcomm_roce_multi_sge
```

运行成功时输出：

```text
[rank 0] hcomm_roce_multi_sge | WriteNbi=128 SGEs=8 | PASS
[rank 1] hcomm_roce_multi_sge | ReadNbi=128 SGEs=8 | PASS
RESULT | Example=hcomm_roce_multi_sge | Status=PASS
```

## 注意事项

- 运行需要至少2张NPU，且每个节点上的MPI本地rank数不超过可用NPU数量。
- rank 0与rank 1之间必须存在RoCE链路，且通道协议使用COMM_PROTOCOL_ROCE。
- CANN安装包必须包含RoCE批量多SGE接口。
- 每4次WriteNbi或ReadNbi调用执行一次BatchCommit和Drain，避免批量WQE超出SQ容量。
- 本地段地址和长度由srcDescs/dstDescs描述，所有段必须位于已注册的通信内存内。
