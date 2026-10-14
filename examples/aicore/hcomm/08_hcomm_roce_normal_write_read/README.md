# Hcomm AIV RoCE WriteNbi/ReadNbi MPI样例

## 概述

本样例在Ascend C AIV Kernel中直接调用Hcomm的RoCE接口，覆盖Init、WriteNbi、ReadNbi、Commit、Drain、Lock和Unlock。Host侧使用MPI管理多进程，负责初始化HCCL通信域、注册通信内存、创建RoCE P2P通道，并在Kernel执行完成后校验数据。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR / Ascend 950DT | >= CANN 9.1.0 |

## 目录结构

```text
08_hcomm_roce_normal_write_read
├── CMakeLists.txt
├── README.md
├── README_en.md
├── hcomm_roce_rw_def.h
├── roce_normal_write_read.asc
└── roce_normal_write_read_kernel.cpp
```

## 功能说明

每个rank只创建到next方向的RoCE通道。Kernel内的调用顺序为：

```cpp
hcomm.Init(...);            // 初始化Hcomm UB工作空间
hcomm.Lock(channel);        // 获取通道锁
hcomm.WriteNbi<false>(...); // 延迟提交RoCE WRITE
hcomm.ReadNbi<false>(...);  // 延迟提交RoCE READ
hcomm.Commit(channel);      // 统一触发doorbell
hcomm.Unlock(channel);      // 刷写通道计数并释放锁
hcomm.Drain(channel);       // 等待CQE完成
```

通信缓冲区分为三段：seg0为本地源数据，seg1接收前一个rank写入的数据，seg2保存从下一个rank读回的数据。Kernel结束后，Host侧逐字节校验seg1和seg2的rank pattern。

## 数据布局

每个rank注册一块4096字节通信内存，其中前768字节参与本次校验：

| 偏移 | 大小 | 用途 |
| --- | ---: | --- |
| `[0, 256)` | 256B | 本rank的源数据 |
| `[256, 512)` | 256B | 接收前一个rank的 `WriteNbi` 结果 |
| `[512, 768)` | 256B | 保存从下一个rank读回的数据 |

Kernel的Hcomm工作空间位于VECOUT UB，大小为512字节。

## 编译

先配置CANN环境，再进入本目录编译：

```bash
cd examples/aicore/hcomm/08_hcomm_roce_normal_write_read
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

使用MPI启动，例如在2个rank上运行：

```bash
cd examples/aicore/hcomm/08_hcomm_roce_normal_write_read
source ${install_path}/cann/set_env.sh
mpirun -n 2 $(pwd)/build/hcomm_roce_normal_write_read
```

运行成功时输出：

```text
[rank 0] hcomm_roce_normal_write_read | sent to rank 1, received from rank 1 | PASS
[rank 1] hcomm_roce_normal_write_read | sent to rank 0, received from rank 0 | PASS
RESULT | Example=hcomm_roce_normal_write_read | Status=PASS
```

## 注意事项

- 运行需要至少2张NPU，且rank数不超过当前可用NPU数量。
- 通道必须使用COMM_PROTOCOL_ROCE，与Kernel侧 `Hcomm<COMM_PROTOCOL_ROCE>` 保持一致。
- HcclGetRootInfo生成的root info由MPI Bcast分发给所有rank。
- WriteNbi和ReadNbi使用commit=false，因此Commit是必要的；Drain返回后才校验数据。
- 当rank数为2时，prev和next是同一个对端，写结果和读结果校验同一个rank pattern。
