# simt_jetty

SIMT 侧 Jetty / DWQE 样例，验证 `thread / warp / group` 三种协作方式，以及多个 warp
并行生成 WQE、group 统一提交的 dispatch 模式。

`HcommPeer` 和 `HcommJetty` 直接从 GM Jetty 表解析队列与远端信息，不需要静态或动态 UB
workspace。固定 SGE 模板参数范围为 2 到 12；地址或长度为 0 的 SGE 不计入 WRITE，并将有效
SGE 压紧，未使用的 WQEBB 使用 NOP 对齐。

同一个 channel 的并发 `Write` 必须属于同一个 batch，并在所有参与 warp 完成后由 group leader
统一调用 `Commit`。所有 lane 构造 `HcommJetty` 后、任一 warp 开始 `Write` 前必须先做一次
group 同步；dispatch 样例使用 12 个有效 SGE 生成 4BB WQE，验证 DWQE 作为 doorbell 发布完整
SQ 的路径。

固定 2BB 的立即 `Put` 在一次调用内由同一份 WQE 数据双写 SQ 和 DWQE，不从 SQ 回读。
`thread / warp / warp-pad / group` 验证该路径；`dispatch` 单独验证多 warp 的 `Write + Commit` 路径。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh
```

没有硬件时，可在编译阶段检查 SIMT VF 的栈和寄存器占用：

```bash
CCE_RES_USAGE=1 bash build.sh
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
bash run.sh 2 warp-pad
bash run.sh 2 group
bash run.sh 2 dispatch
```

`thread` 和 `warp` 都发送两个有效 SGE，用于对比同一个 80B/2BB WQE；`warp-pad` 验证
`1BB WRITE + 1BB NOP`；`dispatch` 验证 12 SGE、4BB WQE。

## 正确性压测

`stress.sh` 在同一个 channel/SQ 上重复执行，每一轮均等待接收端校验完成。接收端校验后会清零
目标缓冲区，避免后一轮未实际下发时被前一轮结果掩盖。默认每个模式执行 100 次：

```bash
bash stress.sh 2
bash stress.sh 2 warp-pad 1000
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
