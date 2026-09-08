# simt_jetty

SIMT 侧 Jetty / DWQE 样例，验证 `thread / warp / group` 三种协作方式，以及多个 warp
并行生成 WQE、group 统一提交的 dispatch 模式。

`HcommPeer` 和 `HcommJetty` 直接从 GM Jetty 表解析队列与远端信息，不需要静态或动态 UB
workspace。固定 SGE 模板参数范围为 2 到 12；地址或长度为 0 的 SGE 保持零填充，未使用的
WQEBB 使用 NOP 对齐。

同一个 channel 的并发 `Write` 必须属于同一个 batch，并在所有参与 warp 完成后由 group leader
统一调用 `Commit`。所有 lane 构造 `HcommJetty` 后、任一 warp 开始 `Write` 前必须先做一次
group 同步；dispatch 样例演示了该调用顺序。

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

`nranks` 必须大于等于 2。样例只实际使用 rank 0 和 rank 1，其余 rank 会直接退出。
