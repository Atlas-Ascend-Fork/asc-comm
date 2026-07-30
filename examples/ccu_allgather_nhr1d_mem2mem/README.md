# CCU AllGather NHR1D Mem2Mem

本样例使用Host直接注册并下发CCU Kernel，演示NHR1D Mem2Mem AllGather数据面。

## 样例介绍

```text
初始化HCCL通信域
  -> 生成NHR step和rank-to-channel映射
  -> 批量申请CCU channel
  -> 注册并下发CCU Kernel
  -> GroupCopy本地slice
  -> 按NHR step远端写入并转发slice
  -> 逐元素校验输出
```

NHR的step数量为`ceil(log2(rankSize))`。第`step`步的通信对象为：

```text
delta    = 1 << (stepNum - 1 - step)
toRank   = (rank + delta) % rankSize
fromRank = (rank + rankSize - delta) % rankSize
```

以4个rank为例：

```text
step 0:
  rank0 -> rank2: slice0
  rank1 -> rank3: slice1
  rank2 -> rank0: slice2
  rank3 -> rank1: slice3

step 1:
  rank0 -> rank1: slice0,slice2
  rank1 -> rank2: slice1,slice3
  rank2 -> rank3: slice2,slice0
  rank3 -> rank0: slice3,slice1
```

每个非末尾step完成后进行对端同步，确保下一步转发的数据已经写入本地输出。

Host和Kernel使用统一的10参数任务ABI，将本轮传输量`chunkSize`与rank间隔
`outputSliceStride`分开，为后续Host多轮分块保留地址布局。

## 支持范围

- 编译架构：`dav-3510`
- CANN版本：9.1或更高版本
- 数据类型：`float`
- 通信范围：单Die、单mission、1～16个rank
- 数据布局：非原地AllGather，各rank的slice大小相同

## 构建和运行

执行前先设置 CANN 运行环境和 CCU 调度模式：

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
cd examples/ccu_allgather_nhr1d_mem2mem
```

构建并运行：

```bash
env DEVICES=0,1,2,3 COUNT=256 ./run.sh
```

## 输入和校验

每个rank的输入为：

```text
input[i] = rank * 1000000 + i + 1
```

输出按照source rank排列，样例会逐元素校验。建议至少覆盖2个、4个和8个rank。

## 结果示例

以下为4个rank、每个rank输入256个`float`元素时的结果示例。4卡场景包含
2个通信步骤，可以体现NHR1D在后一通信步骤中转发已接收数据的过程。不同rank
的打印顺序可能因线程调度而变化。

```text
rank 2 NHR schedule: step0(to=0,from=0,tx=[2]) step1(to=3,from=1,tx=[2,0])
rank 1 NHR schedule: step0(to=3,from=3,tx=[1]) step1(to=2,from=0,tx=[1,3])
rank 3 NHR schedule: step0(to=1,from=1,tx=[3]) step1(to=0,from=2,tx=[3,1])
rank 0 NHR schedule: step0(to=2,from=2,tx=[0]) step1(to=1,from=3,tx=[0,2])
rank 2: launched, waiting for stream
rank 1: launched, waiting for stream
rank 3: launched, waiting for stream
rank 0: launched, waiting for stream
rank 0: stream completed
rank 1: stream completed
rank 2: stream completed
rank 3: stream completed
rank 0 input : [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(256) ]
rank 0 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(1024) ]
rank 0 verified 256 FP32 elements from each of 4 ranks
rank 1 verified 256 FP32 elements from each of 4 ranks
rank 2 verified 256 FP32 elements from each of 4 ranks
rank 3 verified 256 FP32 elements from each of 4 ranks
```
