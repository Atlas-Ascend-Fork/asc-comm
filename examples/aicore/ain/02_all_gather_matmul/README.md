# AIN + Blaze AllGatherMatmul

简体中文 | [English](README_en.md)

本示例在一个 MIX AIC:AIV=1:1 kernel 中组合 AIN `Get` 和 Blaze `BlockMmad`，演示通信与计算重叠。
Host 使用 HCCL 建域和对称 window/team 资源接口，数据面由 AIV 提交 AIN 通信，AIC 执行矩阵乘。

## 语义与边界

每个 rank `r` 提供 `A_r[M,K]` 和 `B_r[K,N]`，得到：

```text
C_r[P*M,N] = Concat(A_0, ..., A_(P-1)) @ B_r
```

`B_r` 可以在各 rank 上不同，输出按源 rank 排序。本版支持：

- Ascend 950PR/950DT、`dav-3510`、单机同一互联组内的 2～64 rank（且不超过可用设备数）。
- FP16/BF16 输入和同类型输出、FP32 累加、ND 连续内存、无转置、无 bias。
- `M>0`，N/K 为正的 16 元素倍数；M 可以不对齐，支持通信和 Cube tile 尾块。
- 独立示例接口，不提供 ACLNN 注册、量化、变长 rank 输入或公开的 gatherOut 输出。

**必须按实际互联拓扑选择设备。** 例如 A5_4p 的 0～3 和 4～7 分属两个互联组，四卡应选择
`--devices 0,1,2,3` 或 `--devices 4,5,6,7`，不能使用 `1,2,3,4`。编号是当前 ACL 环境中的
逻辑 device ID；不要在同时设置可见设备映射后继续使用原物理编号。

## 数据流与同步

1. 为各 rank 的 A 注册发送 window，为 `[P,M,K]` 接收区注册接收 window，创建 AIV/UB_CTP team。
2. 将远端 peer 分配给 AIV，每个本端 peer channel 只有一个提交者和一个完成等待者。
3. AIC 先直接计算本地 `A_r @ B_r`；AIV 同时按 M 分块向所有远端发起 Get。
4. 每轮 AIV 使用 `FlushAsync + Wait` 等待自己的 peer channel，再进行本卡 AIV barrier。
   每个 AIV 通知配对 AIC，本轮远端数据已就绪；AIC 从该轮所有 peer 的 M/N tiles 中分配任务。
5. 下一轮 Get 与本轮远端计算重叠。两个 ready/free 槽限制预取深度；最后排空所有通知。

接收 GM 使用完整 rank-major 空间；数据不会在同一 kernel 内被循环覆盖。本 rank 的接收槽未使用，
本地计算直接读原始 A，远端计算直接读接收 GM，没有额外 pack/unpack。每卡接收空间为 `2*P*M*K` 字节。

AIN 完成等待、本卡核间通知、AIC MTE2 加载顺序分别处理：

- 每个 Get 立即提交，并在下一轮复用同一 channel 前等待完成；单次不超过 256 MiB。
- mode 0 / flag 0 用于 AIV 全核同步；mode 2 / flags 2、3 用于 ready，flags 4、5 用于 free。
  flag ID 不随分块数增长。空闲计算核也参与通知消费和归还。
- 融合 kernel 使用 `__schedmode__(1)`；AIC 等待阻塞 MTE2，接收 A 的 Tensor 关闭 L2 cache hint。
- Host 在每次调用前 barrier、stream 完成后汇集各 rank 时间，保护远端输入生命周期。期间 A 不变。
  TCP 同步调用位于 device event 之外；公共控制通道启用 `TCP_NODELAY`，避免延迟 ACK 造成
  毫秒级启动偏斜。剩余启动偏斜仍可能进入 collective 的等待时间。没有逐轮跨卡 barrier。

计算部分复用 Blaze Basic `BlockMmad`。默认 Cube M/N tile 为 256，K L0/L1 为 64/128、L1 双缓冲。
通信 tile 自动选择为能提供至少一轮 AIC 工作量的 M 分块，并受 M 和 Get 长度限制；也可用
`--tile-m` 固定。非最后一个通信块的行数必须为 256 的倍数。自动值是初始启发式，不是性能最优保证。

## 构建

需要同时包含 AIN API、HCCL team/window API 和相应实现的 CANN 包。实际验证版本见
[VALIDATION.md](VALIDATION.md)，不能只凭版本号推断包中一定带有这些接口。

Blaze 为外部头文件依赖，不复制到 asc-comm，也不修改 ops-transformer。配套依赖版本：

```text
ops-tensor: e6cd0c20c716813ac4c76f2dba7152f63a4514ab
Tensor API submodule: fd51475d8ceb26eb21befffe4391528bc16726a3
```

```bash
CANN_ENV="/path/to/cann/set_env.sh"  # 按实际安装位置修改
OPS_TENSOR_ROOT="/path/to/ops-tensor"
source "$CANN_ENV"
git clone https://gitcode.com/cann/ops-tensor.git "$OPS_TENSOR_ROOT"
git -C "$OPS_TENSOR_ROOT" checkout e6cd0c20c716813ac4c76f2dba7152f63a4514ab
git -C "$OPS_TENSOR_ROOT" submodule update --init --recursive include/tensor_api

# 在本示例目录执行
cmake -S . -B build -DOPS_TENSOR_ROOT="$OPS_TENSOR_ROOT"
cmake --build build -j
```

CMake 检查 Blaze/Tensor API 依赖，通过 CANN ASC 编译，并沿用 Blaze 示例的 host `PIPE_FIX` 兼容头。
ASC 默认目标为 `dav-3510`。本示例无需安装自定义 OPP 包。

## 运行与对照模式

```bash
# 两卡，小 shape 全量随机 golden，串行与融合对照
./build/ain_all_gather_matmul tcp://127.0.0.1:29640 2 257 80 272 \
  --devices 1,2 --tile-m 256 --random --warmup 1 --iters 5

# 四卡稳态测量；按本机实际互联组修改 devices
./build/ain_all_gather_matmul tcp://127.0.0.1:29640 4 1024 4096 4096 \
  --devices 0,1,2,3 --tile-m 256 --dtype bf16 --trace
```

位置参数为 `endpoint ranks M K N`。进程管理器在本机 fork 全部 rank，无需 mpirun。

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `--devices` | `0,...,P-1` | 每个 rank 对应的 ACL device ID，必须唯一且拓扑可达 |
| `--dtype` | `fp16` | `fp16` / `bf16` |
| `--mode` | `both` | `both` 依次运行 serial、fused |
| `--tile-m` | 自动 | 通信 M 分块，末块按真实行数处理 |
| `--cores` | 设备 AIC 数 | 启动核数；可用 1 验证每核多个 peer |
| `--warmup` / `--iters` | 10 / 100 | 预热 / 稳态次数 |
| `--verify-iters` | 2 | 改变输入并验证的次数；测量后再验证最终结果 |
| `--random` | 关闭 | 全 K 随机输入与 O(PMKN) CPU golden，只建议小 shape |
| `--trace` | 关闭 | 为 fused 额外运行一次带系统 cycle 记录的诊断 kernel |
| `--timeout` | 300 秒 | 整个进程组的超时，包含初始化、校验与测量 |
| `--delay-rank` / `--delay-ms` | 不延迟 / 0 | 输入就绪后延迟指定 rank 的启动，检查输入生命周期 |

| mode | 行为 |
| --- | --- |
| `fused` | 一个 MIX kernel 内执行 AIN Get 和 Blaze 计算 |
| `serial` | AIN Get kernel 完成后，启动相同 Blaze 计算 kernel |
| `hccl` | HCCL AllGather 后启动相同 Blaze 计算 kernel；HCCL 使用环境默认算法 |
| `comm` | 独立 AIN Get，校验远端接收数据 |
| `matmul` | Host 填充完整输入，独立验证 Blaze 计算 |

默认数据在 K 上以 16 为周期，rank/行/列/迭代参与数据生成，可在大 shape 下快速校验**所有输出元素**。
`--random` 使用全部 K 索引独立生成数据。两种输入均为可精确表示的 1/16 倍数。CPU golden 使用
FP32 累加并转成输出类型；FP16/BF16 比较阈值分别为 `0.001 + 0.001*abs(expected)` 和
`0.001 + 0.008*abs(expected)`，非有限值判失败。远端接收 A 逐元素按位比较。

`PERF` 记录每次所有 rank device event 耗时的最大值，再统计 median/P95。资源创建、输入复制、
CPU 校验和 TCP 同步均在计时区间外。serial/hccl 的时间包含两个 kernel 间隔。
这是稳态设备执行时间，不是包括 Host 建域的端到端请求延迟，也不是跨设备时钟校准后的全局包络。
`host_submit_skew_p95_us` 记录各 rank 调用起始 event 前的 Host 单调时钟偏斜 P95，用于检查
Host 发射一致性；它不是设备 kernel 启动偏斜，也不从 event 耗时中直接扣除。

`TRACE` 是独立诊断 kernel 中配对 AIV0/AIC0 的系统 cycle 区间；950 上 1000 cycles = 1 us。
计算阶段记录前后同步流水，ready 等待不计入对应远端计算阶段。诊断会改变时序，**不混入 PERF 样本**；
阶段区间相交表示阶段并行，不能解释为网络链路/Cube 单元在每个 cycle 都同时繁忙。不同 rank 的
绝对 cycle 不用于跨卡比较。

## 验证

```bash
# 可在无 CANN 的 macOS/Linux 上运行；不代表 NPU 覆盖
cmake -S . -B /tmp/ain-agmm-host -DAGMM_HOST_TESTS_ONLY=ON
cmake --build /tmp/ain-agmm-host
ctest --test-dir /tmp/ain-agmm-host --output-on-failure

# 目标环境：按实际互联组选择设备，输出目录必须不存在
python3 tests/run_device_tests.py --binary build/ain_all_gather_matmul \
  --devices 0,1,2,3 --output-dir device-results
```

设备测试覆盖 FP16/BF16、M/N/K tile 尾块、18 个通信分块、单核多 peer、延迟 rank、独立通信和
独立计算、HCCL 对照，以及资源复用时改变输入。每条命令和日志保存在输出目录，`results.json`
保留退出码与判定。只有全部 rank 校验通过且进程组成功，才打印最终 `Status=PASS`。

## 手动执行功能与性能测试

以下命令在目标 Linux 主机的同一个 Bash 会话中依次执行。需要远程执行时，先通过 SSH 登录目标主机。
先完成上面的构建，或使用与当前源码、CANN 包匹配的已有二进制。所有路径集中配置，不依赖固定的主机或安装目录。

### 1. 配置环境与保存现场

修改下面的路径和设备列表。`WORK` 是本次测试的工作目录；源码和构建产物可以放在其他位置，
分别通过 `CASE_DIR` 和 `BIN` 指定。本流程使用同一互联组内的四张卡。

```bash
CANN_ENV="/path/to/cann/set_env.sh"
WORK="/path/to/test-work"
CASE_DIR="/path/to/asc-comm/examples/aicore/ain/02_all_gather_matmul"
BIN="$CASE_DIR/build/ain_all_gather_matmul"
DEVICES="4,5,6,7"  # 根据当前 ACL 编号和互联拓扑修改，例如 0,1,2,3
BASE_PORT=30100     # 确保 BASE_PORT 到 BASE_PORT+35 未被其他测试占用

source "$CANN_ENV"
set -e
set -o pipefail     # 测试失败时，即使通过 tee 保存日志，也停止后续步骤
mkdir -p "$WORK"
cd "$WORK"
test -x "$BIN"
test -f "$CASE_DIR/tests/run_device_tests.py"
npu-smi info
ldd "$BIN"
```

上述路径应填写为绝对路径。确认所选四张卡没有其他业务占用，并确认 `ldd` 没有 `not found`，
实际加载的 CANN 库来自预期安装目录，再继续测试。不要为测试停止其他人的进程；卡未释放时等待释放。
如果设置了设备可见性映射，先核实映射后的 ACL device ID。不要跨不互联的四卡组执行。

```bash
RESULT_DIR=$(mktemp -d "$WORK/ain-agmm-results.XXXXXX")
{
  date -Iseconds
  uname -a
  echo "CANN_ENV=$CANN_ENV"
  echo "ASCEND_HOME_PATH=${ASCEND_HOME_PATH:-}"
  echo "BIN=$BIN DEVICES=$DEVICES"
  git -C "$CASE_DIR" rev-parse HEAD
  git -C "$CASE_DIR" status --short
  for component in runtime asc-devkit hccl hcomm; do
    version_file="${ASCEND_HOME_PATH:-}/share/info/$component/version.info"
    echo "component=$component version_file=$version_file"
    if [ -f "$version_file" ]; then
      cat "$version_file"
    else
      echo "版本文件不存在，请按当前安装布局补充记录"
    fi
  done
  sha256sum "$BIN"
  ldd "$BIN"
  npu-smi info
} > "$RESULT_DIR/environment.log" 2>&1
echo "结果目录：$RESULT_DIR"
```

每次重跑都创建新的 `RESULT_DIR`，避免覆盖已有结果。包目录名不一定等于组件版本，应保留组件
版本信息和二进制校验和；源码 HEAD 也不能单独证明已有二进制由该提交构建。

### 2. 功能测试

先运行四卡、小 shape 的随机输入冒烟测试，检查 AIN 串行和融合路径：

```bash
"$BIN" "tcp://127.0.0.1:$BASE_PORT" 4 257 80 272 \
  --devices "$DEVICES" --dtype fp16 --mode both --tile-m 256 \
  --random --verify-iters 2 --warmup 1 --iters 5 --timeout 180 \
  2>&1 | tee "$RESULT_DIR/functional-smoke.log"
```

通过标准是进程退出码为 0、每个 rank 的 `CHECK` 均为 `errors=0`，并出现最终 `Status=PASS`。
随后运行完整设备回归：

```bash
python3 "$CASE_DIR/tests/run_device_tests.py" \
  --binary "$BIN" --devices "$DEVICES" \
  --output-dir "$RESULT_DIR/functional" --port "$((BASE_PORT + 10))" \
  2>&1 | tee "$RESULT_DIR/functional-suite.log"
```

提供四张卡时共运行 11 个用例，覆盖上述验证场景；两卡用例使用设备列表的前两张卡，
四卡用例使用全部四张卡。检查所有用例为 `PASS`，并保留 `functional/results.json` 和各用例日志。
脚本遇到首个失败即退出，未运行用例不能算通过。`--output-dir` 指定的目录必须尚不存在。

### 3. 性能测试：六组对照

功能通过后，在设备空闲且环境一致的条件下，按顺序测试 FP16/BF16 各三种模式：

| 模式 | 对照含义 |
| --- | --- |
| `serial` | AIN AllGather 与 Blaze Matmul 串行执行 |
| `fused` | AIN 与 Blaze Matmul 在同一个 MIX kernel 内重叠 |
| `hccl` | HCCL AllGather 与 Blaze Matmul 串行执行 |

固定每个 rank 的 `M=1024、K=4096、N=1024`，因此四卡输出为 `[4096,1024]`。
每组预热 10 次、测量 100 次，通信分块固定为 256 行，避免自动分块差异影响对照。

```bash
mkdir "$RESULT_DIR/performance"
port=$((BASE_PORT + 30))
for dtype in fp16 bf16; do
  for mode in serial fused hccl; do
    echo "测试：dtype=$dtype mode=$mode port=$port"
    "$BIN" "tcp://127.0.0.1:$port" 4 1024 4096 1024 \
      --devices "$DEVICES" --dtype "$dtype" --mode "$mode" --tile-m 256 \
      --verify-iters 2 --warmup 10 --iters 100 --timeout 600 \
      2>&1 | tee "$RESULT_DIR/performance/${dtype}_${mode}.log"
    port=$((port + 1))
  done
done
npu-smi info > "$RESULT_DIR/devices-after.log"
```

性能测试使用默认周期输入，仍校验所有输出元素；大 shape 不加 `--random`，避免昂贵的 CPU golden。
此处不加 `--trace`；如需观察重叠阶段，另行运行诊断并保存独立日志。不要并发执行不同测试组。

### 4. 汇总与解读

```bash
grep -H '^PERF ' "$RESULT_DIR"/performance/*.log \
  | tee "$RESULT_DIR/performance-summary.txt"
grep -H '^RESULT ' "$RESULT_DIR"/performance/*.log
```

六份日志均应有最终 `Status=PASS`。分别按 dtype 记录三种模式的
`max_rank_median_us`、`max_rank_p95_us` 和 `host_submit_skew_p95_us`，单位均为微秒。
前两项是每次取最慢 rank 后的中位数和 P95，具体计时范围见上文 `PERF` 说明；第三项仅用于
辅助检查 Host 提交偏斜，不能从设备耗时中直接扣除。

在相同 dtype 下，以中位数计算：

- 相对 AIN 串行的耗时降低比例：`(1 - fused / serial) * 100%`。
- 相对 HCCL 串行的耗时降低比例：`(1 - fused / hccl) * 100%`。
- 加速比：`基线耗时 / fused 耗时`；小于 1 表示融合更慢。

仅比较校验通过、设备无其他业务干扰、shape/dtype/卡数/分块和 CANN 环境一致的结果。
若耗时波动明显，保留原始日志，在新的结果目录中重复整套六组测试，不只挑选最快的一轮。
这些步骤用于采集当前环境的结果，本身不代表当前 CANN 包已完成功能或性能验证。

任何特定 shape 的收益都不能推广为全量优于传统 MC2；传统 MC2 算子还需用匹配的 shape、dtype、
布局和通信引擎另行对照。本示例没有把 HCCL cascade 标成传统 MC2 性能。
