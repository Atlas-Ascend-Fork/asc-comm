# CCU AllGather Mesh1D Mem2Mem

本样例使用 Host 直接注册并下发 CCU Kernel，演示 Mesh1D Mem2Mem
AllGather 数据面。

## 样例介绍

```text
Python 生成输入和 Golden 数据
  -> 初始化 HCCL 通信域
  -> 申请 CCU thread 和 UBC_CTP channel
  -> 注册 CCU Kernel
  -> 交换 output 地址和 token
  -> 向所有对端写入本 rank 的 slice
  -> GroupCopy 本地 slice
  -> 等待全部传输完成
  -> C++ 写出各 rank 结果
  -> Python 校验各 rank 输出
```

Channel 通过一次 `HcclChannelAcquire` 批量申请。数据传输使用按 rank 分组的
Event：每 16 个 rank 使用一个 Event，每个 rank 占用一个 Event bit。

Host 和 Kernel 使用统一的 10 参数任务 ABI，将本轮传输量 `chunkSize` 与 rank
间隔 `outputSliceStride` 分开，为后续 Host 多轮分块保留地址布局。

## 支持范围

- 编译架构：`dav-3510`
- CANN 版本：9.1 或更高版本
- 数据类型：`float`
- 通信范围：单 Die、单 mission、1～16 个 rank
- 数据布局：非原地 AllGather，各 rank 的 slice 大小相同

## 构建和运行

执行前先设置 CANN 运行环境和 CCU 调度模式：

```bash
source ${ASCEND_HOME_PATH}/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
cd examples/ccu/ccu_allgather_mesh1d_mem2mem
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

数据生成和校验脚本依赖 Python 3 和 NumPy。`--rank-size` 必须与 `DEVICES`
指定的 Device 数量一致，`--count` 必须与运行算子时的 `COUNT` 一致。

`run.sh` 只负责构建和运行算子，不会自动生成或校验数据。仅构建时执行：

```bash
env BUILD_ONLY=1 ./run.sh
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

每次修改 rank 数量或 `count` 后，都需要重新生成数据，并使用相同参数校验。
例如，4 个 rank、每个 rank 输入 256 个元素时执行：

```bash
python3 scripts/gen_data.py --rank-size 4 --count 256
env DEVICES=0,1,2,3 COUNT=256 ./run.sh
python3 scripts/verify_result.py --rank-size 4 --count 256
```

## 结果示例

以下为 2 个 rank、每个 rank 输入 256 个 `float` 元素时的部分输出。不同 rank
的打印顺序可能因线程调度而变化。

```text
generated 2 rank inputs and golden data, 256 FP32 elements per rank
rank 1: launched, waiting for stream
rank 0: launched, waiting for stream
rank 0: stream completed
rank 1: stream completed
rank 1 input : [ 1000001 1000002 1000003 1000004 1000005 1000006 1000007 1000008 1000009 1000010 1000011 1000012 1000013 1000014 1000015 1000016 ...(256) ]
rank 1 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
rank 1: output saved to <sample_path>/output/output_rank_1.bin
rank 0 input : [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(256) ]
rank 0 output: [ 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 ...(512) ]
rank 0: output saved to <sample_path>/output/output_rank_0.bin
rank 0 verify pass
rank 1 verify pass
test pass!
```
