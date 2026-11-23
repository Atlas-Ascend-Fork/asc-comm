# Asccomm AIV RoCE Atomic/Inline MPI样例

## 概述

本样例在Ascend C AIV Kernel中直接调用Asccomm RoCE普通接口，覆盖`asccomm_atomic_faa_nbi`、`asccomm_atomic_cas_nbi`、`asccomm_write_value_nbi`、`asccomm_init`、`asccomm_commit`和`asccomm_drain`。Host侧使用MPI管理多进程，负责初始化HCCL通信域、注册通信内存、创建RoCE P2P通道，并在Kernel执行完成后校验结果。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR&950DT系列产品 | >= CANN 9.3.0 |

## 目录结构

```text
10_asccomm_roce_atomic_inline
├── CMakeLists.txt
├── README.md
├── README_en.md
├── roce_atomic_inline.asc
├── roce_atomic_inline_def.h
└── roce_atomic_inline_kernel.cpp
```

## 功能说明

rank i 与 rank i + N/2 两两成对通信，其中 N 为 rank 总数。每个rank只创建到唯一对端的RoCE通道。

Kernel内的调用顺序为：

```cpp
asccomm.asccomm_init(...);                                  // 初始化Asccomm UB工作空间
asccomm.asccomm_atomic_faa_nbi<uint64_t, false>(...);      // 延迟提交远端fetch-and-add
asccomm.asccomm_atomic_cas_nbi<uint64_t, false>(...);      // 延迟提交远端compare-and-swap
asccomm.asccomm_write_value_nbi<uint64_t, false>(...);    // 延迟提交inline标量写
asccomm.asccomm_commit<PIPE_S>(channel);                   // 统一触发doorbell
asccomm.asccomm_drain<PIPE_MTE3>(channel);                // 等待CQE完成
```

## 数据布局

每个rank注册一块4096字节通信内存，前40字节参与本测试：

| 偏移 | 初始值 | 操作后预期值 | 用途 |
| --- | ---: | ---: | --- |
| 0 | 0 | 100 | 对端`asccomm_atomic_faa_nbi`写入的远端结果 |
| 8 | 任意 | 任意 | 对端`asccomm_atomic_faa_nbi`在本rank读取的旧值 |
| 16 | 200 | 300 | 对端`asccomm_atomic_cas_nbi`写入的远端结果 |
| 24 | 任意 | 任意 | 对端`asccomm_atomic_cas_nbi`在本rank读取的旧值 |
| 32 | 任意 | 400 | 对端`asccomm_write_value_nbi`写入的远端结果 |

Kernel结束后，Host侧校验本地通信内存中的offset 0、16和32。

## 编译

```bash
cd examples/aicore/asccomm/10_asccomm_roce_atomic_inline
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
cd examples/aicore/asccomm/10_asccomm_roce_atomic_inline
source ${install_path}/cann/set_env.sh
mpirun -n 4 $(pwd)/build/asccomm_roce_atomic_inline
```

运行成功时输出：

```text
[rank 0] asccomm_roce_atomic_inline | peer=2 | PASS
[rank 1] asccomm_roce_atomic_inline | peer=3 | PASS
[rank 2] asccomm_roce_atomic_inline | peer=0 | PASS
[rank 3] asccomm_roce_atomic_inline | peer=1 | PASS
RESULT | Example=asccomm_roce_atomic_inline | Status=PASS
```

## 注意事项

- 运行需要至少2张NPU，rank数必须为偶数且不超过当前可用NPU数量。
- 通道必须使用COMM_PROTOCOL_ROCE，与Kernel侧 `asc::asccomm<asc::COMM_PROTOCOL_ROCE>` 保持一致。
- 三个数据面接口均使用 `commit=false`，`asccomm_commit` 是必要步骤；`asccomm_drain` 返回后才校验数据。
- 本样例验证每个rank被对端修改的远端结果，不校验fetch buffer内容。
