# Hcomm URMA 批量接口性能样例

## 概述

本样例测量 URMA 路径（AIV Kernel 直驱）批量接口（`MakeBatchHandle` + `BatchCommit`）下
`WriteNbi` / `ReadNbi` / `WriteWithNotifyNbi` 三种操作的发送性能，覆盖时延与带宽两个
口径，以及 BatchCommit 是否计入时延计时段的两种提交方式。

两个 rank 对称收发：双方都向对端发起通信，各自校验本地落点。这是有意为之——两方
并发会互相争抢链路和 SQ 资源，测出的数值是"双流并发"口径；与单发送方口径（如
[`05_simt_hcomm_urma_perftest`](../05_simt_hcomm_urma_perftest/README.md)）不可直接对比。

通信拓扑由 `--perf_type` 选择（默认 `p2p`，即上述两卡对称收发）：

- `p2p`：只有 rank 0/1 参与通信；`--ranks` 大于 2 时其余 rank 空闲——照常进通信域、
  只挂集合点，不注册内存、不建链、不起核（可观察更大通信域内 p2p 是否受干扰）。
- `ring`：单向环 0→1→...→N-1→0，每张卡把 `write`/`notify` 发给下一张卡、用 `read`
  从上一张卡拉数据；每张卡都有人发、也都被上一卡写，校验仍各查各的落点。
  2 卡 `ring` 退化为 `p2p`。

## 本样例支持的产品及CANN软件版本

| 产品 | CANN软件版本 |
| --- | --- |
| Ascend 950PR&950DT系列产品 | >= CANN 9.3.0 |

## 目录结构介绍

```text
13_hcomm_urma_batch_api_perf
├── CMakeLists.txt               // 编译工程文件
├── urma_batch_api_perf.asc      // Host侧通信域、内存注册、逐档循环和计时输出
├── urma_batch_api_perf_kernel.cpp  // AIV Kernel侧URMA批量接口调用与计时
├── urma_batch_api_perf_def.h    // Host与Kernel共享定义
├── README.md                    // 中文样例说明
└── README_en.md                 // 英文样例说明
```

## 样例描述

### 功能说明

三种操作 × 两种口径 × 两种提交方式：

| `api` | 说明 |
| --- | --- |
| `write` | `WriteNbi`（批句柄重载），本卡 source 区 → 对端 write 区 |
| `read` | `ReadNbi`（批句柄重载），本卡 read 区 ← 对端 source 区 |
| `notify` | `WriteWithNotifyNbi`（批句柄重载），本卡 source 区 → 对端 notify 区，payload 之后另写一个远端 flag word |

| `metric` | 说明 |
| --- | --- |
| `lat` | 时延：逐轮分段计时，计时段内只有发起（`commit=true` 时含 BatchCommit）、不含 Drain——纯提交路径时延；BatchCommit 与 Drain 都在段外（每批之间窗外 Drain 一次，SQ 每轮从头起跑） |
| `bw` | 带宽：单窗口包住全部轮次的发起 + 每批 `BatchCommit` + 末尾一次 Drain——端到端带宽 |

| `commit` | 说明 |
| --- | --- |
| `true` | BatchCommit **计入**时延计时段（默认） |
| `false` | BatchCommit 挪到每轮停表之后发——**不计入**时延计时段 |

带宽口径不区分提交方式：BatchCommit 本身始终包含在带宽计时段内。

### 循环模型

- **rounds（默认 1）** = 外层「批轮次」重复次数；
- **batchCap（默认 1024）** = 每批请求条数，**决定地址窗口大小**；
- 每一轮 batch 写的都是**同一扇地址窗口**：地址只由批内 index `0~batchCap-1` 决定
  （`dst + i*len` / `src + i*len`；notify 的 flag 是固定单槽，不随 index 推进），轮次
  不推进地址；
- host 精度校验只查**一个批窗口**——每轮写的数据相同（源窗口 pattern 恒定）。

数据正确性：每条请求按批内 index 落不同偏移，host 校验整个批窗口——性能与功能一次覆盖。

CQE 策略：**每批的最后一条请求用 cqe=1**（Drain 按累计 CQE 数轮询、SQ 回收都依赖它），
其余 cqe=0——中间不产生 CQE 干扰。

计时窗口内不判错、不提前退出：返回码直接覆盖存（零比较零分支），出窗后统一判断上报。
计时值由 kernel 写入 context 回传 host，host 侧用全精度 cycles 重算均值与派生列：

- `AvgCycles = Cycles / (Rounds × BatchSize)`
- `SingleCoreTime/us = AvgCycles / 1000`（Ascend950 系统计数器 1GHz）
- `Bandwidth/GB/s = DataSize / SingleCoreTime / 1000`

### 内存布局

单块 **1GB** 注册内存按一档 `(len, batchCap)` 划分，各区大小都是一个批窗口：

| 偏移 | 区域 |
| --- | --- |
| 0 | source：本卡发送源 / 对端 `ReadNbi` 的读源（批内按 i 推进） |
| batchCap×len | write：对端 `WriteNbi` 的落点（第 i 条写 `base + i*len`） |
| 2×batchCap×len | read：本卡 `ReadNbi` 读回的落点 |
| 3×batchCap×len | notify：对端 `WriteWithNotifyNbi` 的落点 |
| 4×batchCap×len（8B 对齐后） | flag：notify flag **单槽**（固定地址，仅 8B） |

要点：

- **同一扇批窗口**：write/notify 的源与落点都按批内 index 推进；read 的落点按 index
  推进、源固定在对端 source 窗口首条（同值反复读）。轮次（rounds）不改变地址——每轮
  写同样的数据，host 只校验一个窗口。notify 的 flag 固定单槽 + 固定值 `300`。
- **批容量上限**（超限报错退出、不钳制——窗口大小由 batchCap 决定，钳小会导致 host
  校验窗口与实际不符）：
  - VECOUT UB：batch 暂存缓冲 `batchCap × 每条WQEBB × 64B ≤ 192KB` 预算，kernel 侧
    运行期校验（`WriteWithNotifyNbi` 占 2 WQEBB、其余占 1）；
  - SQ：时延档每批之间窗外 Drain，SQ 每批从头起跑，须容纳一批
    `batchCap × 每条WQEBB < sqDepth`；默认档 1024 不超过默认 sqDepth(4096) 环的一半，
    超限时靠 Post/BatchCommit 返回码在出计时窗后报错；
  - 注册内存：`4 × batchCap × len + 8 ≤ 1GB`，kernel 侧运行期校验。
- 初值：source 区填 `100 + myRank`（int32 pattern），落点区 + flag 区清 0。

### 逐档串行执行：launch → 校验 → 重填 → 下一档

```
for batch in {min, min×2, ..., max}:        # --batch-range，默认只 1024 一档
  for len in {2^minExp, ..., 2^maxExp}:     # --exponent-range，默认 8B..128KB
    重铺初值（source 区一个批窗口填 pattern + 落点区清 0）
    barrier                          # 两卡都铺完再放行
    launch 本档核函数 → sync
    barrier                          # 等对端也收尾完
    校验本档数据（一个批窗口）
    档末两卡合并判定 → 任一卡失败则双卡都停止    # 后续档会重铺内存覆盖现场，
                                                 # 且单方停止会让对端卡死在同步点
```

单个核函数承载全部维度（消息长度 / 轮数 / 批容量 / 接口 / 口径 / 提交方式都是运行期入参）。

### Host 侧校验

每档校验（一律读本地，只查一个批窗口），只校验本次选中的接口：

- write / read / notify 三区整个批窗口 `batchCap×len` 全部 == `100 + peerRank`；
- notify flag 单槽 == `300`（固定值）：单槽被一批请求并发写，WQE in-order completion
  只约束完成上报、不保证远端 flag 写的落地顺序（实测 batch=1024 时槽里留的值不确定），
  逐条值不可校验；固定值与完成顺序无关，校验 == 常量即确认 notify 路径通。

比较按 4B（int32）粒度。

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | `--perf_type` 选拓扑：`p2p`（两卡对称收发，默认）或 `ring`（单向环收发） |
| 调用方式 | AIV Kernel 内批量接口（`MakeBatchHandle` + `BatchCommit`） |
| 测试接口 | `write`、`read`、`notify` |
| 性能口径 | `lat`（提交时延）、`bw`（端到端带宽） |
| 提交方式 | `true`（BatchCommit 计入时延，默认）、`false`（不计入） |
| 支持rank数 | 2..N（`ring` 全员参与；`p2p` 实际只有 rank 0/1 通信，其余空闲） |

## 编译运行

在本样例根目录下执行如下步骤，编译并运行样例。本样例仅支持NPU运行模式。

- 配置环境变量

  请根据当前环境上CANN开发套件包的[安装方式](../../../../docs/quick_start.md)配置环境变量。

  ```bash
  source ${install_path}/cann/set_env.sh
  ```

  > **说明：** `${install_path}`为CANN包安装目录，未指定安装目录时默认安装至`/usr/local/Ascend`。

- 样例执行

  在本样例目录下执行如下命令。可执行文件内部fork出全部rank进程，直接运行即可：

  ```bash
  mkdir -p build
  cd build
  cmake -DCMAKE_ASC_ARCHITECTURES=dav-3510 ..
  make -j
  cd ..
  ./build/urma_batch_api_perf tcp://127.0.0.1:29627            # 默认 2p p2p
  ./build/urma_batch_api_perf tcp://127.0.0.1:29627 --perf_type ring --ranks 4
  ```

- 启动参数说明

  ```bash
  ./urma_batch_api_perf <tcp://ip:port> [options]
  ```

  | 参数 | 默认值 | 说明 |
  | --- | --- | --- |
  | `<tcp://ip:port>` | 无默认 | rank 0监听的控制通道地址，用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `--ranks` | `2` | 拉起的rank进程数（≥2）；`p2p` 下大于2时其余rank空闲，`ring` 下全员参与 |
  | `--perf_type` | `p2p` | 通信拓扑：`p2p`（rank 0/1 对称收发）或 `ring`（单向环，每卡发下一卡、拉上一卡，如 4 卡即 0→1→2→3→0） |
  | `--exponent-range` | `3 17` | 数据量幂数范围（数据量 = 2^exp，含两端，如 3 17 = 8B..128KB） |
  | `--exponent` | 无 | 单个幂数（等价 `--exponent-range exp exp`，只跑一档） |
  | `--api` | `all` | 测哪些操作：`all`或`write`/`read`/`notify`逗号分隔 |
  | `--metric` | `all` | 时延/带宽口径：`all`或`lat`/`bw`逗号分隔 |
  | `--commit` | `true` | 两种模式 BatchCommit 触发时机相同（每批末尾一次），区别是算不算进时延计时段；带宽不区分 |
  | `--rounds` | `1` | 批轮次重复次数（warmup 固定 1 个完整批轮次，不对外开放） |
  | `--batch-range` | `1024 1024` | 每批请求条数范围（min 起每档 ×2 至 max，含两端；决定地址窗口大小） |
  | `--batch` | 无 | 单个批大小（等价 `--batch-range N N`；上限由 kernel 运行期校验） |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR&950DT系列产品 |

- 执行结果

  ```text
  PERF_DATA | Rank=0 | API=WriteNbi | DataSize/B=4096 | BatchSize=1024 | Commit=true | Topology=p2p | Metric=lat | Rounds=1 | Cycles=148480 | AvgCycles=145.0000 | SingleCoreTime/us=0.145000 | Bandwidth/GB/s=28.1375
  [rank 0] case=batch1024_4096B validation: PASS (checked 4096KB per region)
  RESULT | Example=urma_batch_api_perf | Status=PASS
  ```

  每个「操作 × 口径」组合打一行 `PERF_DATA`；全部档位跑完后 rank 0 把结果汇总写
  `perf_data.csv`（运行目录下，含 `topology` 列记录本次拓扑）。
  `RESULT`行为`PASS`且各档校验均`PASS`表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 批容量约束：VECOUT UB 预算与注册内存窗口在 kernel 侧运行期校验，SQ 深度超限靠
  Post/BatchCommit 返回码在出计时窗后报错；超限均报错退出、不钳制——窗口大小由
  batchCap 决定，钳小会导致 host 校验窗口与实际不符。
- 默认档 1024：`WriteWithNotifyNbi` 每条占 2 WQEBB，1024×2=2048 不超过默认
  sqDepth(4096) 环的一半。
- 设备内存需求：每卡 1GB 注册内存（最坏一档 batch=1024 × len=128KB ≈ 512MB 落点区）。
- 编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
