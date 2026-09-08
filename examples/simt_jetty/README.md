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

`nranks` 必须大于等于 2。样例只实际使用 rank 0 和 rank 1，其余 rank 会直接退出。
