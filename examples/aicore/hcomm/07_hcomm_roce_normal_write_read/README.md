# Hcomm AIV RoCE WriteNbi/ReadNbi MPI样例

## 概述

本样例在Ascend C AIV Kernel中直接调用Hcomm的RoCE接口，覆盖Init、WriteNbi、ReadNbi、Commit、Drain、Lock和Unlock。Host侧使用MPI管理多进程，负责初始化HCCL通信域、注册通信内存、创建RoCE P2P通道，并在Kernel执行完成后校验数据。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR&950DT系列产品 | >= CANN 9.3.0 |

## 目录结构

```text
07_hcomm_roce_normal_write_read
├── CMakeLists.txt
├── README.md
├── README_en.md
├── hcomm_roce_rw_def.h
├── roce_normal_write_read.asc
└── roce_normal_write_read_kernel.cpp
```

## 功能说明

rank i 与 rank i + N/2 两两成对通信，其中 N 为 rank 总数：

- 2 卡：rank 0 ↔ rank 1
- 4 卡：rank 0 ↔ rank 2，rank 1 ↔ rank 3
- 8 卡：rank 0 ↔ rank 4，rank 1 ↔ rank 5，rank 2 ↔ rank 6，rank 3 ↔ rank 7

每个rank只创建到唯一对端的RoCE通道。Kernel内的调用顺序为：

```cpp
hcomm.Init(...);            // 初始化Hcomm UB工作空间
hcomm.Lock(channel);        // 获取通道锁
hcomm.WriteNbi<false>(...); // 延迟提交RoCE WRITE
hcomm.ReadNbi<false>(...);  // 延迟提交RoCE READ
hcomm.Commit(channel);      // 统一触发doorbell
hcomm.Unlock(channel);      // 刷写通道计数并释放锁
hcomm.Drain(channel);       // 等待CQE完成
```

通信缓冲区分为三段：seg0为本地源数据，seg1接收对端rank写入的数据，seg2保存从对端rank读回的数据。Kernel结束后，Host侧逐字节校验seg1和seg2的rank pattern。

## 数据布局

每个rank注册一块4096字节通信内存，其中前768字节参与本次校验：

| 偏移 | 大小 | 用途 |
| --- | ---: | --- |
| `[0, 256)` | 256B | 本rank的源数据 |
| `[256, 512)` | 256B | 接收对端rank的 `WriteNbi` 结果 |
| `[512, 768)` | 256B | 保存从对端rank读回的数据 |

Kernel的Hcomm工作空间位于VECOUT UB，大小为512字节。

## 编译

先配置CANN环境，再进入本目录编译：

```bash
cd examples/aicore/hcomm/07_hcomm_roce_normal_write_read
source ${install_path}/cann/set_env.sh
export MPI_HOME=/usr/local/mpich
mkdir build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

如果要使能 `ASCENDC_DEBUG`，请使用 `cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 -DASCENDC_DEBUG=ON ..`。

## 使能MPI环境变量

此处内容根据实际环境中MPI安装路径进行调整。

```bash
export MPI_HOME=/usr/local/mpich
export PATH="${MPI_HOME}/bin:${PATH}"
export LD_LIBRARY_PATH="${MPI_HOME}/lib:${LD_LIBRARY_PATH:-}"
```

## 运行

使用MPI启动，rank数必须是大于等于2的偶数：

```bash
cd examples/aicore/hcomm/07_hcomm_roce_normal_write_read
source ${install_path}/cann/set_env.sh
mpirun -n 4 $(pwd)/build/hcomm_roce_normal_write_read
```

`-n 4` 表示启动4个MPI进程，即4个通信rank；每个进程默认使用同编号的NPU。4可按实际NPU数量替换，必须是大于等于2的偶数。

运行成功时输出：

```text
[rank 0] hcomm_roce_normal_write_read | peer=2 | PASS
[rank 1] hcomm_roce_normal_write_read | peer=3 | PASS
[rank 2] hcomm_roce_normal_write_read | peer=0 | PASS
[rank 3] hcomm_roce_normal_write_read | peer=1 | PASS
RESULT | Example=hcomm_roce_normal_write_read | Status=PASS
```

## 注意事项

- 运行需要至少2张NPU，rank数必须为偶数且不超过当前可用NPU数量。
- 通道必须使用COMM_PROTOCOL_ROCE，与Kernel侧 `Hcomm<COMM_PROTOCOL_ROCE>` 保持一致。
- HcclGetRootInfo生成的root info由MPI Bcast分发给所有rank。
- WriteNbi和ReadNbi使用commit=false，因此Commit是必要的；Drain返回后才校验数据。
- 每个rank只与 rank i + N/2 建立通道，读写结果均校验该对端的rank pattern。
