# Hcomm URMA 普通接口性能样例

## 概述

本样例测量 URMA 路径（AIV Kernel 直驱）上三个点对点普通接口的发送性能：`WriteNbi`、
`ReadNbi`和`WriteWithNotifyNbi`，每种接口覆盖时延与带宽两个口径、立即提交与延迟提交
两种发布策略。

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
12_hcomm_urma_normal_api_perf
├── CMakeLists.txt               // 编译工程文件
├── urma_normal_api_perf.asc     // Host侧通信域、内存注册、逐档循环和计时输出
├── urma_normal_api_perf_kernel.cpp  // AIV Kernel侧URMA接口调用与计时
├── urma_normal_api_perf_def.h   // Host与Kernel共享定义
├── README.md                    // 中文样例说明
└── README_en.md                 // 英文样例说明
```

## 样例描述

### 功能说明

三种接口 × 两种口径 × 两种提交方式：

| `api` | 说明 |
| --- | --- |
| `write` | `WriteNbi`，本卡 source 区 → 对端 write 区 |
| `read` | `ReadNbi`，本卡 read 区 ← 对端 source 区 |
| `notify` | `WriteWithNotifyNbi`，本卡 source 区 → 对端 notify 区，payload 之后另写一个远端 flag word |

| `metric` | 说明 |
| --- | --- |
| `lat` | 时延：计时段内只有发起循环，末尾不含 Drain——纯提交路径时延（`final`方式的末尾 Commit 也在段外） |
| `bw` | 带宽：发起全部完成后、末尾一次 Drain 等收尾完成才停表——端到端带宽 |

| `commit` | 说明 |
| --- | --- |
| `inline` | 接口模板入参 `commit=true`，每次发起即敲 doorbell |
| `final` | 接口模板入参 `commit=false` 只写 SQE，每段计时结束后统一一次 `Commit`——时延口径里 Commit 在计时段外，带宽口径里末尾 Commit + Drain 在计时段内 |

计时流程为：warmup 次（`min(100, rounds)`）发起后 `Drain`（不计时），随后正式发起
`rounds` 次并记录计时段 cycles。时延口径计时段内只有发起（`final`方式的 Commit 在段外）；
带宽口径计时段内含收尾（`final`方式为「末尾 Commit + Drain」，`inline`方式为「末尾 Drain」）。

计时窗口内不判错、不提前退出：返回码直接覆盖存（零比较零分支），出窗后统一判断上报，
测量路径与错误路径彻底分离。计时值由 kernel 写入 context 回传 host，host 侧用全精度
cycles 重算均值与派生列：

- `AvgCycles = Cycles / Rounds`
- `SingleCoreTime/us = AvgCycles / 1000`（Ascend950 系统计数器 1GHz）
- `Bandwidth/GB/s = DataSize / SingleCoreTime / 1000`

### 内存布局

各档共用同一块 **2GB** 注册内存，按绝对偏移划分：

| 偏移 | 大小 | 区域 |
| --- | --- | --- |
| 0 | 256MB | source：本卡发送源 / 对端 `ReadNbi` 的读源 |
| 256MB | 512MB | write：对端 `WriteNbi` 的落点（随轮次推进 `dst + i*len`） |
| 768MB | 512MB | read：本卡 `ReadNbi` 读回的落点（随轮次推进） |
| 1280MB | 512MB | notify：对端 `WriteWithNotifyNbi` 的落点（随轮次推进） |
| 1792MB | 8B | flag：notify flag **单槽**（固定地址） |

要点：

- **源固定、落点推进**：Write/Notify 的 src 固定在本卡 source 区；目的按轮次推进，
  避免自覆盖干扰测量；Read 的 src 在对端 source 区、落回本卡 read 区推进。
- **轮数上限**：每档要求 `rounds * len ≤ 512MB`（落点区容量），host 侧逐档校验，
  超限直接报错退出。
- 初值：source 区填 `100 + myRank`（int32 pattern），落点区 + flag 区清 0。

### 逐档串行执行：launch → 校验 → 重填 → 下一档

```
for len in {2^minExp, ..., 2^maxExp}:   # 数据量 = 2^exp，逐档指数 +1，含两端
    重铺初值（落点区清 0 + source 区重填 pattern）
    barrier                       # 两卡都铺完再放行
    launch 本档核函数 → sync
    barrier                       # 等对端也收尾完
    校验本档数据
    档末两卡合并判定 → 任一卡失败则双卡都停止   # 后续档会重铺内存覆盖现场，
                                                # 且单方停止会让对端卡死在同步点
```

单个核函数承载全部维度（消息长度 / 接口 / 口径 / 提交方式 / 轮数都是运行期入参）。

### Host 侧校验

2GB 不做 host 全量影像：只留一块 256MB 暂存区（`aclrtMallocHost`），铺初值时用它下发
source，校验时用它分片把落点区拷回来比。每档校验（一律读本地），只校验本次选中的接口：

- source 区仍 == `100 + myRank`（没被写坏）；
- write / read / notify 三区**实际写到的 `rounds*len` 区间**全部 == `100 + peerRank`；
- notify flag 单槽 == `300`（固定值）：单槽被 rounds 个请求并发写，WQE in-order
  completion 只约束完成上报、不保证远端 flag 写的落地顺序，逐条值不可校验；固定值与
  完成顺序无关，校验 == 常量即确认 notify 路径通（全体漏发时槽是初值 0，可检出）。

比较按 4B（int32）粒度。

### 样例规格

| 项目 | 说明 |
| --- | --- |
| 通信模式 | `--perf_type` 选拓扑：`p2p`（两卡对称收发，默认）或 `ring`（单向环收发） |
| 调用方式 | AIV Kernel 内直接调用 URMA 接口 |
| 测试接口 | `write`、`read`、`notify` |
| 性能口径 | `lat`（提交时延）、`bw`（端到端带宽） |
| 发布策略 | `inline`（默认）、`final` |
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
  ./build/urma_normal_api_perf tcp://127.0.0.1:29626            # 默认 2p p2p
  ./build/urma_normal_api_perf tcp://127.0.0.1:29626 --perf_type ring --ranks 4
  ```

- 启动参数说明

  ```bash
  ./urma_normal_api_perf <tcp://ip:port> [options]
  ```

  | 参数 | 默认值 | 说明 |
  | --- | --- | --- |
  | `<tcp://ip:port>` | 无默认 | rank 0监听的控制通道地址，用于root info交换与Host侧barrier；并行运行多个实例时错开端口 |
  | `--ranks` | `2` | 拉起的rank进程数（≥2）；`p2p` 下大于2时其余rank空闲，`ring` 下全员参与 |
  | `--perf_type` | `p2p` | 通信拓扑：`p2p`（rank 0/1 对称收发）或 `ring`（单向环，每卡发下一卡、拉上一卡，如 4 卡即 0→1→2→3→0） |
  | `--exponent-range` | `3 17` | 数据量幂数范围（数据量 = 2^exp，含两端，如 3 17 = 8B..128KB） |
  | `--exponent` | 无 | 单个幂数（等价 `--exponent-range exp exp`，只跑一档） |
  | `--api` | `all` | 测哪些接口：`all`或`write`/`read`/`notify`逗号分隔 |
  | `--metric` | `all` | 时延/带宽口径：`all`或`lat`/`bw`逗号分隔 |
  | `--commit` | `inline` | `inline`（每发起一次即commit）/ `final`（commit=false + 每段末尾统一一次Commit；Commit不计入时延、计入带宽） |
  | `--rounds` | `1000` | 每档计时轮数（warmup 固定 `min(100, rounds)`，不对外开放） |

- 编译选项说明

  | 选项 | 可选值 | 说明 |
  | --- | --- | --- |
  | `CMAKE_ASC_RUN_MODE` | `npu`（默认） | 运行模式，本样例仅支持NPU运行 |
  | `CMAKE_ASC_ARCHITECTURES` | `dav-3510` | NPU架构，对应Ascend 950PR&950DT系列产品 |

- 执行结果

  ```text
  PERF_DATA | Rank=0 | API=WriteNbi | DataSize/B=4096 | Commit=inline | Topology=p2p | Metric=lat | Rounds=1000 | Cycles=145000 | AvgCycles=145.0000 | SingleCoreTime/us=0.145000 | Bandwidth/GB/s=28.1375
  [rank 0] case=4096B validation: PASS (checked 3MB per region)
  RESULT | Example=urma_normal_api_perf | Status=PASS
  ```

  每个「接口 × 口径」组合打一行 `PERF_DATA`；全部档位跑完后 rank 0 把结果汇总写
  `perf_data.csv`（运行目录下，含 `topology` 列记录本次拓扑）。`RESULT`行为`PASS`且
  各档校验均`PASS`表示样例执行成功。

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 每档落点区容量 512MB：`rounds * len` 超过该值的参数组合会在启动时报错退出
  （减小 `--rounds` 或收窄 `--exponent-range`）。
- 时延口径计时段内不 Drain，`rounds` 个任务须一次性驻留 SQ；SQ 深度超限时靠
  Post 返回码在出计时窗后报错（`WriteWithNotifyNbi` 占 2 WQEBB、其余占 1）。
- 编译依赖CANN ASC CMake能力，并在链接阶段依赖CANN `hcomm`库。
