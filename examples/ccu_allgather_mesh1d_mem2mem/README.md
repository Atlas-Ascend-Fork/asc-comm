# CCU AllGather Mesh1D Mem2Mem

本样例使用Host直接注册并下发CCU Kernel，演示Mesh1D Mem2Mem AllGather数据面。

## 样例介绍

```text
初始化HCCL通信域
  -> 申请CCU thread和UBC_CTP channel
  -> 注册CCU Kernel
  -> 交换output地址和token
  -> 向所有对端写入本rank的slice
  -> GroupCopy本地slice
  -> 等待全部传输完成
  -> 逐元素校验输出
```

Channel通过一次`HcclChannelAcquire`批量申请。数据传输使用按rank分组的Event：
每16个rank使用一个Event，每个rank占用一个Event bit。

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
cd examples/ccu_allgather_mesh1d_mem2mem
```

构建并运行：

```bash
env DEVICES=0,1 COUNT=256 ./run.sh
```

## 输入和校验

每个rank的输入为：

```text
input[i] = rank * 1000000 + i + 1
```

输出按照source rank排列，样例会逐元素校验。可使用以下命令覆盖单rank、尾块和
多rank场景：

```bash
env DEVICES=0 COUNT=256 ./run.sh
env DEVICES=0,1 COUNT=65539 ./run.sh
env DEVICES=0,1,2,3 COUNT=256 ./run.sh
```

## 结果示例

以下为2个rank、每个rank输入256个`float`元素时的部分输出。不同rank的打印
顺序可能因线程调度而变化。

```text
rank 1: launched, waiting for stream
rank 0: launched, waiting for stream
rank 0: stream completed
rank 1: stream completed
rank 1 input : [ 1000001 1000002 1000003 1000004 1000005 1000006 1000007 1000008 1000009 1000010 1000011 1000012 1000013 1000014 1000015 1000016 ...(256) ]
rank 1 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
rank 1 verified 256 FP32 elements from each of 2 ranks
rank 0 input : [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(256) ]
rank 0 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
rank 0 verified 256 FP32 elements from each of 2 ranks
```
