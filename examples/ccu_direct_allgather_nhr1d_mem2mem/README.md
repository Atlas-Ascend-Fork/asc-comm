# CCU Direct AllGather NHR1D Mem2Mem

该样例将 `asc-devkit/pr_4261` 的 NHR1D Mem2Mem AllGather 数据面迁移到
`asc-comm`，使用 Host 直接注册和下发 CCU kernel：

```text
Host 建域、申请 CCL buffer/thread/channel
  -> 生成 NHR step 和 rank-to-channel 映射
  -> HcommCcuKernelRegister
  -> HcommCcuKernelLaunch
  -> 本地 GroupCopy + NHR 分步远端 Write
  -> 逐元素校验 AllGather 输出
```

本样例不使用 KFC server、AICore HCCL client、InitV2、selector/executor 或
OpResCtx 控制面。

## NHR 调度

NHR step 数为 `ceil(log2(rankSize))`。每一步使用 PR-4261 的模运算调度：

```text
delta    = 1 << (stepNum - 1 - step)
toRank   = (rank + delta) % rankSize
fromRank = (rank + rankSize - delta) % rankSize
```

以 4 rank 为例：

```text
step 0:
  rank0 -> rank2: [slice0]       rank2 -> rank0: [slice2]
  rank1 -> rank3: [slice1]       rank3 -> rank1: [slice3]

step 1:
  rank0 -> rank1: [slice0,slice2]
  rank1 -> rank2: [slice1,slice3]
  rank2 -> rank3: [slice2,slice0]
  rank3 -> rank0: [slice3,slice1]
```

每一步的远端写完成后进行 step 同步，避免下一步转发尚未到达的 slice。

## 支持范围

- Ascend 950PR/DT，`dav-3510`
- CANN 9.1+
- FP32
- 单 Die、单 mission、非 in-place
- 1 到 16 个 rank
- 每个 rank 的 slice 大小相同

2 rank 可以验证 direct-launch 和基本 NHR 数据路径；至少 4 rank 才会转发前一步
收到的 slice。

## 构建和运行

先加载 CANN 环境：

```bash
source /path/to/cann/set_env.sh
cd examples/ccu_direct_allgather_nhr1d_mem2mem
```

直接构建并运行双卡：

```bash
env DEVICES=0,1 COUNT=256 ./run.sh
```

四卡验证多步转发：

```bash
env DEVICES=0,1,2,3 COUNT=256 ./run.sh
```

如果 qrun 的执行时间有限，可以先在队列外构建：

```bash
env BUILD_ONLY=1 ./run.sh
qrun ./build/ccu_direct_allgather_nhr1d_mem2mem \
  --devices 0,1,2,3 --count 256
```

`run.sh` 默认设置：

```text
DEVICES=0,1
COUNT=256
BUILD_DIR=./build
HCCL_OP_EXPANSION_MODE=CCU_SCHED
```

CANN plog 默认写入本样例的：

```text
log/debug/plog/
```

## 输入和校验

每个 rank 的输入为：

```text
input[i] = rank * 1000000 + i + 1
```

每个 rank 的输出按 source rank 排列，并进行逐元素校验。建议覆盖：

```bash
env DEVICES=0 COUNT=256 ./run.sh
env DEVICES=0,1 COUNT=1 ./run.sh
env DEVICES=0,1 COUNT=8192 ./run.sh
env DEVICES=0,1 COUNT=65539 ./run.sh
env DEVICES=0,1,2 COUNT=256 ./run.sh
env DEVICES=0,1,2,3 COUNT=256 ./run.sh
```

真机验证报告应记录 CANN 版本、SoC、设备列表、NHR schedule 日志和每个 rank
的校验结果。
