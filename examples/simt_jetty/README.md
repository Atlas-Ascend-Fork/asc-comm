# simt_jetty

SIMT 侧 Jetty / DWQE 样例，验证 `thread / warp / group` 三种协作方式，以及多个 warp
并行生成 WQE、group 统一提交的 dispatch 模式。

`HcommPeer` 和 `HcommJetty` 直接从 GM Jetty 表解析队列与远端信息，不需要静态或动态 UB
workspace。SGE 模板参数在编译时配置，范围为 1 到 12；地址或长度为 0 的 SGE 不计入 WRITE，并将有效
SGE 压紧，未使用的 WQEBB 使用 NOP 对齐。

同一个 channel 的并发 `Write` 必须在同一组同步中完成，并在所有参与 warp 完成后由 group leader
统一调用 `Commit`。`Write` 默认立即提交；使用 `Write<N, false>` 时只构造 WQE，等待显式
`Commit`。所有 lane 构造 `HcommJetty` 后、任一 warp 开始 `Write` 前必须先做一次
group 同步；dispatch 样例中每个 warp 生成一个含 12 个有效
SGE 的 4BB WQE，再由 group 统一 Commit 和 PublishSq。

立即 `Write` 按 payload 大小生成对应数量的 WQEBB，并在一次调用内由同一份 WQE 数据双写 SQ 和
DWQE，不从 SQ 回读。`thread / warp / warp-lane / group` 验证该路径；`dispatch` 单独验证多 warp 的
`Write + Commit` 路径。

`mixed` 使用 DeepEP 风格的 `__global__ __mix__` 入口：SIMT VF 只组织并完成 SQ，随后由 SIMD/AIV
主体读取共享 SQ head 并通过普通 doorbell 发布。SIMD 和 SIMT 共享同一个 80B 元数据对象。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh
```

通过编译参数选择 thread/mixed 使用的 `Write<N>` 模板参数。每次构建只实例化一个 `Write<N>`，例如：

```bash
SIMT_JETTY_SGE_NUM=2 bash build.sh
SIMT_JETTY_SGE_NUM=4 bash build.sh
SIMT_JETTY_SGE_NUM=6 bash build.sh
SIMT_JETTY_SGE_NUM=12 bash build.sh
```

没有硬件时，可在编译阶段检查 SIMT VF 的栈和寄存器占用：

```bash
CCE_RES_USAGE=1 bash build.sh
```

调试构建使用 ASC-Comm 统一的 `ASCENDC_DEBUG` 宏启用 SIMT Jetty 参数、地址和队列状态校验；Release
默认编译掉这些判断：

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel 4
```

## 执行

```bash
bash run.sh 2
```

默认按独立 communicator/channel 依次执行所有模式。也可以只执行一个模式，避免失败用例污染后续
SQ/CQ 状态：

```bash
bash run.sh 2 thread
bash run.sh 2 warp
bash run.sh 2 warp-lane
bash run.sh 2 group
bash run.sh 2 dispatch
```

`thread` 和 `mixed` 使用编译时配置的 SGE 数量；`warp` 发送两个有效 SGE，
用于验证同一个 80B/2BB WQE；`warp-lane` 使用编译期 SGE 数量，以 DeepEP 的 32-lane/64-bit
映射写入固定 4BB slot；默认构建为 2 SGE 时可直接与 `warp` 对比；`dispatch` 验证
12 SGE、4BB WQE。

运行时 `--sge-num` 是可选的一致性检查；thread/mixed 必须与构建时的
`SIMT_JETTY_SGE_NUM` 相同。不传时直接使用二进制中的编译配置：

```bash
bash run.sh 2 thread --sge-num 6
bash run.sh 2 mixed --sge-num 6
bash run.sh 2 thread
```

## 正确性压测

`stress.sh` 在同一个 channel/SQ 上重复执行，每一轮均等待接收端校验完成。接收端校验后会清零
目标缓冲区，避免后一轮未实际下发时被前一轮结果掩盖。默认每个模式执行 100 次：

```bash
bash stress.sh 2
bash stress.sh 2 warp 1000
bash stress.sh 2 warp-lane 1000
```

也可以直接通过通用脚本指定次数：

```bash
bash run.sh 2 all --iterations 100
```

## 性能测试

`perf.sh` 使用设备侧 `clock()` 打点。性能循环单独调用 `Write` 组 WQE，在 `Write` 返回后记录
`write_cycles`，再调用 `Commit` 并单独记录 `commit_cycles`，最后在 `Drain` 返回后记录完整完成
耗时 `completion_cycles`。因此 `average_write_us` 不包含 commit 时间。默认 warmup 100 次，随后
计时 1000 次。每个 warmup/计时样本单独 launch 一个 kernel，与压力测试使用相同的执行边界；
每轮都会 Drain，确保 SQ tail 持续推进。host launch 和同步时间不计入上述设备周期：

```bash
bash perf.sh 2
bash perf.sh 2 warp 10000 1000
```

结果中的周期按当前设备 1 GHz 时钟换算为微秒，与 `simt_urma_perftest` 的统计方式一致。

`nranks` 必须大于等于 2。样例只实际使用 rank 0 和 rank 1，其余 rank 会直接退出。
