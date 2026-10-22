# CCU AllGather NHR1D Mem2Mem

本样例使用 ASC `<<<>>>` 直调方式下发 CCU Kernel，演示 NHR1D Mem2Mem
AllGather 数据面。

## 概述

```text
Python 生成输入和 Golden 数据
 -> 初始化 HCCL 通信域
 -> 生成 NHR step 和 rank-to-channel 映射
 -> 批量申请 CCU channel
  -> 通过 `<<<>>>` 直调 CCU Kernel
  -> GroupCopy 本地 slice
  -> 按 NHR step 远端写入并转发 slice
  -> C++ 写出各 rank 结果
  -> Python 校验各 rank 输出
```

NHR 的 step 数量为 `ceil(log2(rankSize))`。第 `step` 步的通信对象为：

```text
delta    = 1 << (stepNum - 1 - step)
toRank   = (rank + delta) % rankSize
fromRank = (rank + rankSize - delta) % rankSize
```

以2个rank为例：

```text
step 0:
  rank0 -> rank1: slice0
  rank1 -> rank0: slice1
```

每个非末尾 step 完成后进行对端同步，确保下一步转发的数据已经写入本地输出。

Host 和 Kernel 使用统一的 10 参数任务 ABI，将本轮传输量 `chunkSize` 与
rank 间隔 `outputSliceStride` 分开，为后续 Host 多轮分块保留地址布局。

## 本样例支持的产品及CANN软件版本

- 编译架构：`dav-3510`
- CANN 版本：9.1 或更高版本
- 数据类型：`float`
- 通信范围：单 Die、单 mission、1～16 个 rank
- 数据布局：非原地 AllGather，各 rank 的 slice 大小相同

## 目录结构介绍

```text
03_allgather_nhr1d_mem2mem/
├── CMakeLists.txt
├── README.md
├── README_en.md
├── run.sh
├── main.asc
└── scripts/
    ├── gen_data.py
    └── verify_result.py
```

## 样例描述

本样例基于HCCL通信域申请CCU资源，并通过ASC `<<<>>>` 直调CCU Kernel完成数据通信或搬运。

## 编译运行

执行前先设置 CANN 运行环境和 CCU 调度模式：

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
cd examples/ccu/ccu_direct/03_allgather_nhr1d_mem2mem
```

生成输入和 Golden 数据：

```bash
python3 scripts/gen_data.py --rank-size 2 --count 256
```

构建并运行算子：

```bash
env DEVICES=0,1 COUNT=256 ./run.sh
```

校验各 rank 的输出：

```bash
python3 scripts/verify_result.py --rank-size 2 --count 256
```

## 输入和校验

`scripts/gen_data.py` 为每个 rank 生成一个 FP32 输入文件：

```text
input/input_rank_<rank_id>.bin
```

每个 rank 的输入为：

```text
input[i] = rank * 1000000 + i + 1
```

Golden 数据按 source rank 顺序拼接并写入 `output/golden.bin`。C++ 程序将各
rank 的执行结果写入 `output/output_rank_<rank_id>.bin`，随后由
`scripts/verify_result.py` 逐元素校验。

数据生成和校验脚本依赖 Python 3 和 NumPy。`--rank-size` 必须与
`--devices` 指定的 Device 数量一致，`--count` 必须与运行算子时一致。

`run.sh` 不会自动生成或校验数据。每次修改 rank 数量或 `count` 后，都需要
重新生成输入和 Golden 数据。

## 执行结果

以下为2个rank、每个rank输入256个`float`元素时的结果示例。2卡场景包含
1个通信步骤。不同rank的打印顺序可能因线程调度而变化。

```text
rank 1 NHR schedule: step0(to=0,from=0,tx=[1])
rank 0 NHR schedule: step0(to=1,from=1,tx=[0])
rank 1: launched, waiting for stream
rank 0: launched, waiting for stream
rank 0: stream completed
rank 1: stream completed
rank 0 input : [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(256) ]
rank 0 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
rank 0: output saved to <sample_path>/output/output_rank_0.bin
rank 1: output saved to <sample_path>/output/output_rank_1.bin
rank 0 verify pass
rank 1 verify pass
test pass!
```

## 注意事项

- 运行样例需要至少2张NPU；单卡环境仅支持编译验证。
- 运行前必须设置`HCCL_OP_EXPANSION_MODE=CCU_SCHED`。
- 当前样例默认使用`dav-3510`架构，仅适用于Ascend 950PR&950DT系列产品。
