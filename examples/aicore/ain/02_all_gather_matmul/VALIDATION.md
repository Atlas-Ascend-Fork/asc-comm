# AIN + Blaze AllGatherMatmul 验证记录

验证日期：2026-09-24。结论针对本示例当前工作区版本，不代表传统 MC2 算子或所有 shape。

## 环境和版本

| 项目 | 实际值 |
| --- | --- |
| asc-comm 基线 | `master`，`79de7e489db8db78f8fe3b616682099a888a204d`，叠加本次示例和 TCP 控制通道修改 |
| ops-tensor | `e6cd0c20c716813ac4c76f2dba7152f63a4514ab` |
| Tensor API submodule | `fd51475d8ceb26eb21befffe4391528bc16726a3` |
| 目标 | SSH `A5_4p`，Linux x86_64，Ascend950PR，28 AIC |
| Device | 两卡 `0,1`；四卡 `0,1,2,3` |
| CANN 环境入口 | `/mnt/docker/jxy/cann/cann/set_env.sh` |
| 实际 ASCEND_HOME_PATH | `/mnt/docker/jxy/cann/cann-9.3.0` |
| 安装清单版本 | runtime / asc-devkit / hccl / hcomm 均为 `Version=9.2.0`，`timestamp=20260923_000324205` |
| 驱动工具 | `npu-smi 25.7.rc1.6` |
| 构建 / 启动 | CANN ASC，`dav-3510`；示例本机 fork，无 MPI、无自定义 OPP 安装 |

目录名与安装清单版本不一致，不能把本次验证简称为“CANN 9.3 验证”。`ldd` 确认 HCOMM、HCCL、
runtime 等库来自上述实际 CANN 路径。AIN 头文件和运行库使用该安装包；本次没有重新构建整套 asc-comm。
Blaze 编译保留一条外部 `dispatch_policy.h:67` 的 `cce_global attribute ignored` 警告，最终构建成功。

远端独立工作目录为 `/mnt/docker/jxy/ain-blaze-agmm.GilOAt`。源码位于 `src`，构建产物位于 `build`。
执行后 `npu-smi` 显示 0～3 卡无运行进程；5～7 卡的其他用户进程未改动。测试并非整机独占性能验收。

## 构建和正确性

- macOS arm64：便携 Host CMake/CTest 通过，覆盖参数、溢出、尾块地址、数值转换和 golden；不代表设备执行。
- Linux x86_64 + ASC：最终版本编译成功，包含普通和独立 cycle 采样 kernel。
- NPU：最终版本 **11/11 用例通过**，所有参与 rank 校验成功、进程退出码 0。

| 用例 | Rank | M / K / N | 重点 |
| --- | --- | --- | --- |
| matmul_random | 2 | 17 / 80 / 272 | 独立 Blaze，完整 K 随机参考 |
| get_random | 2 | 257 / 80 / 272 | 独立 Get，逐元素按位校验 |
| fused_fp16_tail | 2 | 257 / 80 / 272 | FP16，M/N/K Cube 尾块 |
| fused_bf16_tail | 2 | 513 / 144 / 272 | BF16，完整 K 随机参考 |
| many_rounds | 2 | 4353 / 80 / 272 | 18 轮通信，循环复用 ready/free flag |
| one_core | 2 | 257 / 80 / 272 | 单个 AIC/AIV 配对 |
| delayed_peer | 2 | 257 / 80 / 272 | rank 1 输入就绪后延迟 20 ms 启动 |
| hccl_cascade | 2 | 257 / 80 / 272 | HCCL AllGather + 相同 Blaze 计算 |
| four_rank_fp16 | 4 | 513 / 144 / 528 | FP16 四卡多 peer |
| four_rank_bf16 | 4 | 257 / 80 / 272 | BF16 四卡、完整 K 随机参考 |
| multiple_peers_per_core | 4 | 257 / 80 / 272 | 单个 AIV 提交并等待多个 peer channel |

每项默认改变输入验证两次，再预热 1 次、测量 5 次并校验最终输出。除独立模式和 HCCL 用例外，
同时覆盖 AIN 串行与融合版本。远端接收 A 按位检查；C 全元素检查，阈值见 README。

复现命令（在远端工作目录执行）：

```bash
source /mnt/docker/jxy/cann/cann/set_env.sh
cmake -S src/examples/aicore/ain/02_all_gather_matmul -B build \
  -DOPS_TENSOR_ROOT="$PWD/ops-tensor"
cmake --build build -j4
python3 src/examples/aicore/ain/02_all_gather_matmul/tests/run_device_tests.py \
  --binary build/ain_all_gather_matmul --devices 0,1,2,3 \
  --output-dir device-tests-rerun --port 29800
```

此前误用 `1,2,3,4` 的四卡测试在资源初始化失败：`HcclTeamCreate` 返回 6/1、
`HcclCommSymWinRegister` 返回 9，尚未进入计算 kernel。按用户提供的 0～3 / 4～7 拓扑改用
`0,1,2,3` 后通过。失败日志保留于 `device-tests/four_rank_fp16.log`，不计入验收或性能统计。

## 稳态性能

四卡 `0,1,2,3`，每 rank `M=1024, K=4096, N=1024`，`tileM=256`，28 核，预热 10 次、
测量 100 次。每次取所有 rank 的 device event 耗时最大值，再统计 median/P95；没有混入诊断采样。

| dtype | 模式 | median / us | P95 / us | Host 提交偏斜 P95 / us |
| --- | --- | ---: | ---: | ---: |
| FP16 | AIN serial | 408.699 | 410.609 | 6.129 |
| FP16 | AIN fused | 257.151 | 260.299 | 6.039 |
| BF16 | AIN serial | 409.139 | 412.402 | 10.416 |
| BF16 | AIN fused | 257.781 | 262.249 | 6.119 |
| FP16 | HCCL cascade | 491.656 | 505.657 | 6.049 |

该 shape 的融合版本相对 AIN 串行版本，中位耗时分别降低 **37.081% / 36.994%**，
加速比分别为 **1.589x / 1.587x**。两者采用相同 Blaze 计算逻辑；结果含 kernel 间隔与实际调度代价。
HCCL cascade 使用环境默认通信算法，不等同于 ops-transformer 的传统 MC2 融合算子。

```bash
# FP16；BF16 使用端口 29821 并增加 --dtype bf16
./build/ain_all_gather_matmul tcp://127.0.0.1:29820 4 1024 4096 1024 \
  --devices 0,1,2,3 --tile-m 256 --mode both --trace --timeout 600
# HCCL cascade
./build/ain_all_gather_matmul tcp://127.0.0.1:29822 4 1024 4096 1024 \
  --devices 0,1,2,3 --tile-m 256 --mode hccl --timeout 600
```

### TCP 同步造成的旧基线偏差

修复前，公共 TCP 控制通道对消息头、载荷分开发送，未禁用 Nagle。四进程独立 probe 发现
连续 barrier 的 rank 退出偏斜约 40,980～41,000 us；旧 HCCL cascade 中位耗时为 42,627.992 us。
这组数据存在明显启动偏斜，不能用于加速比结论。

给已连接 socket 设置 `TCP_NODELAY` 后，相同 probe 的 10 次偏斜为 11.177～47.361 us；
最终算子测量另行记录了上表的 Host 提交偏斜。HCCL cascade 随之恢复至 491.656 us。
保留 `sync_probe.cpp`、`sync-before.log`、`sync-after.log` 和旧 `perf-*.log` 供复核。

TCP 调用位于 event 外部，并不意味着启动偏斜不会进入 collective 等待。Host 提交偏斜也不等于
设备 kernel 启动偏斜；这里没有直接扣除偏斜或声称已取得跨设备校时后的全局包络。

## 通信计算重叠证据

每种 dtype 额外执行一次带 cycle 记录的 kernel，采集每个 rank 的配对 AIV0/AIC0，
共 72 个完整阶段区间。950 上按 1000 cycles/us 转换，各 rank 单独计算区间交集：

- local matmul 与 Get 第 0 轮重叠。
- remote matmul 第 j 轮与 Get 第 j+1 轮重叠，j=0、1、2。
- FP16 的 16 个交集为 **41.677～42.947 us**，BF16 为 **41.813～44.413 us**。

例如 FP16 rank 0：本地计算与 Get 第 0 轮交集 41.840 us；远端计算第 0 轮与 Get 第 1 轮
交集 42.947 us。区间直接来自 `perf-final-fp16.log`，未使用跨 rank 的绝对 cycle 差。

这是阶段并行证据；采样 kernel 会改变流水时序，且只记录 core 0，不能据此声称所有核、网络链路
和 Cube 在整个交集内均忙碌。本次未采集完整 msprof 任务时间线。

## 证据位置与限制

本地原始证据目录：
`/Users/izanaami/Documents/MC2_AICPU/logs/ain_blaze_agmm_20260924_GilOAt`。

- `device-tests-final/results.json`：最终 11 项完整命令、退出码及日志路径。
- `build-final.log`、`host-tests.log`：目标构建与便携测试分别记录。
- `perf-final-{fp16,bf16,hccl}.log`：最终性能、每 rank 校验和原始 cycle。
- `audit_results.py`、`audit-summary.json`：校验 11 项结果、5 行性能和 32 个正交集。
- `environment-final.log`、`package-versions.log`、`npu-after.log`：包路径、依赖和设备状态。
- `source-sha256.json`、`binary.sha256`：参与构建的示例/公共工具源码及二进制摘要；本地与远端源码比对一致。

已验证 2/4 rank，未验证跨互联组、8/16 rank、950DT 或其他 shape 的性能范围。参数允许的 rank
上限不是设备验收范围。未进行 Get/Put A/B、完整传统 MC2 对照、转置/量化/ACLNN 接入和大规模
随机压力测试；当前 tile 参数也未做全面调优。本记录对应完成上板验证时的源码，交付状态以关联 PR 为准。
