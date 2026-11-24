# Asccomm AIV RoCE 批量接口性能样例

## 概述

本样例基于 09_asccomm_roce_batch_write_read 改造，用于测量 RoCE 批量接口在不同数据量下的性能。
默认执行 write_nbi 全流程，数据量从 8B 开始，按 2 倍递增到 16MB；可通过 --api 参数切换为 read_nbi。
同一批请求不重叠：第 i 次请求使用 i * AlignUp(dataSize, 64) 字节偏移。

默认通过 `--mode batch` 测试批量接口；通过 `--mode normal` 可切换到普通接口。两种模式都先执行 100 次 warmup，再对后续 100 次取平均值。

`--mode batch` 每轮包含以下阶段：

1. make_batch_handle
2. write_nbi 或 read_nbi 默认执行 1024 次，次数由 --batch-size 配置；前 N-1 次 cqe=0，第 N 次 cqe=1
3. batch_commit
4. drain

第 N 次请求生成整批唯一的 CQE，使 drain 可以等待本批请求全部完成。

`--mode normal` 每轮包含以下阶段：

1. init
2. write_nbi 或 read_nbi：前 999 次延迟提交且 cqe=0，第 1000 次 commit=true 且 cqe=1
3. 前 999 次请求与第 1000 次 commit=true 请求作为一个整体计时，不单独统计提交阶段
4. drain

batch 模式记录单次请求下发、“batch 次请求加 1 次提交”的单请求平均耗时、batch_commit、drain 以及用于吞吐计算的整轮耗时；normal 模式仅记录“全部请求加最后一次提交”的单请求平均耗时、drain 和吞吐。耗时均以微秒（us）为单位，按 1 cycle = 1 ns 换算。


## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
|------|-------------|
| Ascend 950PR&950DT系列产品 | >= CANN 9.3.0 |

## 目录结构

    11_asccomm_roce_api_perf
    ├── CMakeLists.txt
    ├── roce_api_perf.asc // MPI、ACL、HCCL 通道、计时和输出
    ├── roce_api_perf_kernel.cpp // AIV Kernel 批量接口调用和分段计时
    ├── roce_api_perf_def.h // Host/Kernel 共享常量和上下文
    ├── parse_perf_data.py // 解析运行输出并生成表格
    ├── README.md
    └── README_en.md

## 数据布局

启动时按 --max-bytes 和 --batch-size 一次性计算、分配并注册连续通信内存，注册大小至少为 4KB。
--min-bytes 和 --max-bytes 可在 8B 到 16MB 范围内自行配置，注册内存大小随 --max-bytes 自适应。
总注册大小为 max(4096, 2 * batchSize * AlignUp(maxBytes, 64))。前半部分是 send 区域，后半部分是
recv 区域；两个区域都按 AlignUp(maxBytes, 64) 分成 batchSize 个请求槽位。Write 时本端 send 槽位是
源地址，对端 recv 槽位是目的地址；Read 时本端 recv 槽位是目的地址，对端 send 槽位是源地址。
同一批请求按 AlignUp(dataSize, 64) 递增槽位偏移，始终基于初始注册地址偏移，不为每次请求注册新的内存。
send 区域按 rank 和地址偏移生成可区分的数据模式，用于发现请求写错槽位或数据错位。

## 编译

    cd examples/aicore/asccomm/11_asccomm_roce_api_perf
    source `CANN安装路径`/set_env.sh
    export MPI_HOME=/usr/local/mpich
    mkdir build
    cd build
    cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
    make -j

如果要使能 `ASCENDC_DEBUG`，请使用 `cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 -DASCENDC_DEBUG=ON ..`。

编译本样例要求 MPI C++ 编译器（mpicxx）可用。

RoCE 后端通过 CMake 选项 `ASCCOMM_ROCE_BACKEND_MLX5` 选择：不开启该选项时，使用默认的 1825 网卡；开启该选项时，使用 mlx5 网卡。该选项默认开启，可通过 `-DASCCOMM_ROCE_BACKEND_MLX5=OFF` 或 `-DASCCOMM_ROCE_BACKEND_MLX5=ON` 显式指定。

## 使能MPI环境变量

此处内容根据实际环境中MPI安装路径进行调整。

```bash
export MPI_HOME=/usr/local/mpich
export PATH="${MPI_HOME}/bin:${PATH}"
export LD_LIBRARY_PATH="${MPI_HOME}/lib:${LD_LIBRARY_PATH:-}"
```

## 运行

    cd examples/aicore/asccomm/11_asccomm_roce_api_perf
    source `CANN安装路径`/set_env.sh
    mpirun -n 2 $(pwd)/build/asccomm_roce_api_perf 2>&1 | tee run.log

可选参数：

    mpirun -n 2 $(pwd)/build/asccomm_roce_api_perf\
      --mode batch --api write --batch-size 1024 --min-bytes 8 --max-bytes 16777216 2>&1 | tee run.log

运行 batch 版本：

    mpirun -n 2 $(pwd)/build/asccomm_roce_api_perf\
      --mode batch --api write --batch-size 1024 --min-bytes 8 --max-bytes 16777216 2>&1 | tee run.log

运行普通版本：

    mpirun -n 2 $(pwd)/build/asccomm_roce_api_perf\
      --mode normal --api write --min-bytes 8 --max-bytes 16777216 2>&1 | tee run.log

运行输出为 PERF_DATA 记录，包含每个 rank、接口、数据量、各阶段耗时和吞吐量：

    Rank=0 | Mode=batch | API=write_nbi | DataBytes=256 | BatchSize=1024 | IssuePerRequest(Us)=... | IssueCommitPerRequest(Us)=... | batch_commit(Us)=... | drain(Us)=... | DeviceTime(Us)=... | DeviceGBps=...
    Rank=0 | Mode=normal | API=write_nbi | DataBytes=256 | LoopSize=1000 | IssueCommitPerRequest(Us)=... | drain(Us)=... | DeviceTime(Us)=... | DeviceGBps=...
    RESULT | Example=asccomm_roce_api_perf | Status=PASS

--api 可选值为 write 和 read，默认值为 write。--mode 可选值为 batch 和 normal，默认值为 batch。
--batch-size 仅控制 batch 模式的每轮请求数；normal 模式固定执行 1000 次请求。

## 结果导出

将运行日志解析为 CSV 表格：

    python3 parse_perf_data.py --csv run.log

默认在当前目录生成 perf_data.csv。

## 注意事项

- 至少需要 2 张 NPU，rank 数不超过当前可用 NPU 数。
- 通道必须使用 COMM_PROTOCOL_ROCE，与 Kernel 侧 `asc::asccomm<asc::COMM_PROTOCOL_ROCE>` 保持一致。
- HcclGetRootInfo 生成的 root info 通过 MPI Bcast 分发给所有 rank。
- 默认完整运行会在每个 rank 上注册 32GB 连续设备内存（send 与 recv 各 16GB）；通过 --max-bytes 指定更小上限时，注册内存按该上限自适应缩小。
- 所有通信地址要求 64 字节对齐。
- Write 和 Read 完成后都校验每个请求槽位前 min(dataSize, 256) 字节；Read 前会先清空本端接收槽位，
  避免残留数据影响判断。
