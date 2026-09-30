# Hcomm AIV RoCE WriteNbi/ReadNbi MPI样例

## 概述

本样例在Ascend C AIV Kernel中直接调用Hcomm的RoCE接口，覆盖Init、WriteNbi、ReadNbi、Commit、Drain、Lock和Unlock。Host侧使用MPI管理多进程，负责初始化HCCL通信域、注册通信内存、创建RoCE P2P通道，并在Kernel执行完成后校验数据。

## 目录结构

```text
06_hcomm_roce_normal_write_read
├── CMakeLists.txt
├── README.md
├── README_en.md
├── hcomm_roce_rw_def.h
├── op_kernel.cpp
└── main.cpp
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

## 编译运行

先配置CANN环境，再进入本目录编译：

```bash
source ${install_path}/cann/set_env.sh
mkdir -p build
cd build
cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
make -j
```

使用MPI启动，例如在2个rank上运行：

```bash
cd ..
mpirun -n 2 ./build/hcomm_roce_normal_write_read
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
