# CCU ReduceScatter 直接执行样例

## 样例介绍

本样例使用 HCCL 通信域资源、CCU channel 和 `ccu::WriteReduce` 实现单机 mesh ReduceScatter SUM。
它走的是 CCU 数据面 kernel 直发路径，不是 MC2 AscendC 算子路径。

接口语义与 `HcclReduceScatter` 一致：参数 `recvCount` 是每个 rank 的输出元素数；每个 rank 的输入元素数为
`recvCount * rankSize`，输入按目标 rank 顺序分成 `rankSize` 段。

当前实现范围：

- 仅支持 `HCCL_DATA_TYPE_FP32` 和 `HCCL_REDUCE_SUM`，其它组合会明确返回不支持。
- 支持不同的 `sendBuf` 和 `recvBuf`，不支持原地操作。
- 支持最多 16 个 rank 的单机 1D mesh。
- 每个通信域执行一次，资源申请方式与 `custom_ccu_allreduce` 相同，未实现 engine context 缓存复用。

## 输入输出语义

设通信域大小为 `N`，每个 rank 的输出元素数为 `recvCount`。每个 rank 都必须提供
`recvCount * N` 个输入元素，并按目标 rank 连续分段：

```text
sendBuf = [segment_0 | segment_1 | ... | segment_N-1]
             |             |
             |             `-- 归约到 rank N-1
             `---------------- 归约到 rank 0

每个 segment 包含 recvCount 个元素。
```

逻辑上，ReduceScatter 先将所有 rank 的完整 `sendBuf` 逐元素归约，再把归约结果均分成 `N` 段。
rank `p` 只保留第 `p` 段，因此：

```text
rank p 的 recvBuf = SUM(所有 rank 的 segment_p)
```

缓冲区大小为：

```text
sendSize = recvCount * rankSize * sizeof(dataType)
recvSize = recvCount * sizeof(dataType)
```

本实现不会先生成完整的归约结果再执行 Scatter，而是直接把 `segment_p` 发送并归约到 rank `p`，
把 Reduce 和 Scatter 融合成一次通信。

## 完整执行流程

### 1. 初始化进程和测试数据

`main.cc` 中的主线程执行以下步骤：

1. 调用 `aclInit` 初始化 ACL。
2. 调用 `aclrtGetDeviceCount` 获取设备数，也就是本样例的 `rankSize`。
3. 在 rank 0 对应设备上调用 `HcclGetRootInfo` 生成所有 rank 共享的 root info。
4. 为每个设备启动一个 Host 线程，每个线程执行一次 `Sample`。
5. 等待全部线程结束，根据各线程的 `PASS/FAIL` 返回程序结果。

每个 `Sample` 线程绑定一个 NPU，创建自己的 HCCL communicator 和 ACL stream。测试固定使用：

```text
recvCount = 2
sendCount = recvCount * rankSize
sendBuf[i] = rankId * 100 + i
```

这种初始化会让不同目标 rank 得到不同结果。如果错误地把 ReduceScatter 实现成 AllReduce，测试不会误通过。

### 2. 组装 ReduceScatter 参数

`HcclReduceScatterCustom` 对指针、数据类型、归约类型和 rank 数进行校验，然后填充 `OpParam`：

```text
stream      ACL 任务流
inputPtr    sendBuf
outputPtr   recvBuf
recvCount   每个 rank 最终接收的元素数
dataType    当前只支持 FP32
reduceOp    当前只支持 SUM
myRank      当前 communicator 中的 rank ID
rankSize    communicator 中的 rank 数
```

这里的 `recvCount` 不是输入总元素数。输入总元素数始终是 `recvCount * rankSize`。

### 3. 申请 CCU 资源

`AllocAlgResource` 完成 CCU kernel 运行前的资源准备：

1. 使用 `HcclThreadAcquireWithStream` 申请一个绑定当前 ACL stream 的 CCU thread。
2. 遍历除自己之外的所有 peer rank。
3. 使用 `HcclRankGraphGetLinks` 查找本 rank 到 peer rank 的 `COMM_PROTOCOL_UBC_CTP` 链路。
4. 每个 peer 申请一个 CCU channel，channel 顺序按 peer rank 递增排列并跳过自己。
5. 查询 communicator 对应的 CCU instance。
6. 注册 `CcuReduceScatterMesh1DMem2MemKernel`，把 `rankSize`、`rankId` 和 channel handles 固化到 kernel 参数中。

CCU kernel 因此有两类参数：

- 注册阶段参数：通信拓扑相关，包含 rank 和 channel，kernel 注册时确定。
- 启动阶段参数：本次数据地址、token、长度和循环参数，每次 launch 时传入。

### 4. Host 侧计算地址和分片

`ExecOp` 根据 `recvCount` 计算：

```text
rankSliceSize = recvCount * sizeof(float)
inputSize     = rankSliceSize * rankSize
outputSize    = rankSliceSize
```

随后分别为整个输入区和整个输出区调用 `HcommCcuGetMemToken`。输入和输出可能是两块独立的 Device
内存，因此必须使用两个 token，不能像旧 AllReduce 样例那样假设二者共享 token。

UB 协议单次处理上限为 256 MiB。超过上限时，Host 按每个 rank 的 segment 内偏移进行多次 launch。
第 `loop` 次 launch 的基地址为：

```text
inputAddr  = sendBuf + processedDataCount * sizeof(float)
outputAddr = recvBuf + processedDataCount * sizeof(float)
```

这里的 `inputAddr` 仍然对应所有 segment 的相同子区间。CCU kernel 再通过
`rankIdx * rankSliceSize` 找到每个目标 rank 的完整 segment。

每次 launch 传入 10 个 `uint64_t` 参数：

| 序号 | 参数 | 含义 |
| --- | --- | --- |
| 0 | `inputAddr` | 当前分片在 segment 0 中的输入基地址 |
| 1 | `outputAddr` | 当前分片在本地输出中的基地址 |
| 2 | `inputToken` | 覆盖完整 `sendBuf` 的 CCU token |
| 3 | `outputToken` | 覆盖完整 `recvBuf` 的 CCU token |
| 4 | `rankSliceSize` | 输入中相邻两个 rank segment 的字节跨度 |
| 5 | `sliceSize` | 本次 launch 实际处理的字节数 |
| 6-9 | `goSize` | 本地拷贝 LoopGroup 的主体和尾块参数 |

### 5. CCU kernel 加载资源和动态参数

kernel 启动后先根据注册参数恢复资源：

1. 为每个 peer 从 channel 取得远端输出地址变量和远端输出 token 变量。
2. 使用 `ccu::LoadArg` 按 Host 侧相同顺序加载 10 个动态参数。
3. 本 rank 的输入地址和 input token 只在本地使用，不需要发送给 peer。

### 6. 交换输出地址和 token

每个 rank 通过 `ccu::WriteVariableWithNotify` 向所有 peer 发布：

```text
本 rank 的 outputAddr
本 rank 的 outputToken
```

随后在每条 channel 上执行 `NotifyWait`。等待完成后，每个 rank 都知道应该向每个 peer 的哪块输出内存写数据，
并持有访问该内存所需的 token。

### 7. 初始化本地输出

rank `r` 自己负责初始化本地 `recvBuf`：

```text
localSrc = inputAddr + r * rankSliceSize
localDst = outputAddr
```

`GroupCopy` 使用 CCU buffer 和 LoopGroup，把本 rank 输入中的 `segment_r` 当前分片拷贝到本地输出。
该数据是最终 SUM 的第一个操作数。

本地拷贝结束后记录并等待本 rank 对应的 event bit，保证本地输出已经具备有效初值。

### 8. 初始化屏障

所有 rank 在每条 channel 上执行一次 `NotifyRecord/NotifyWait` 屏障。这个屏障不能省略：远端
`WriteReduce` 必须发生在目标 rank 完成本地初始化之后，否则本地普通拷贝可能覆盖已经到达的归约结果。

### 9. 执行远端 WriteReduce

rank `r` 遍历所有目标 rank `p`，跳过自己。发送地址和目标地址分别是：

```text
remoteRankSrc = inputAddr + p * rankSliceSize
remoteRankDst = rank p 发布的 outputAddr
```

然后执行：

```cpp
ccu::WriteReduce(channelToP,
                 remoteRankDst,
                 remoteRankSrc,
                 sliceSize,
                 HCCL_DATA_TYPE_FP32,
                 HCCL_REDUCE_SUM,
                 event,
                 rankMask);
```

也就是说，rank `r` 的 `segment_p` 只会被累加到 rank `p`，不会广播到其它 rank。
等待 `remoteMask` 对应的所有 event bit 后，本 rank 发出的远端归约全部完成。

### 10. 后同步和结果检查

kernel 最后在所有 channel 上执行后同步，确保 peer 的远端写入也已经完成，之后才允许 Host 观察输出。
Host 调用 `aclrtSynchronizeStream` 等待 kernel，拷回 `recvBuf`，计算期望值并打印 `PASS/FAIL`。

输出前按照仓库其它集合通信样例的方式等待 `rankId` 秒，使多个 Host 线程按 rank 顺序打印。

## 两卡数据流示例

当 `rankSize = 2`、`recvCount = 2` 时：

```text
rank 0 sendBuf = [0,   1 | 2,   3]
rank 1 sendBuf = [100, 101 | 102, 103]
                   |             |
                   |             `-- segment_1，归约到 rank 1
                   `---------------- segment_0，归约到 rank 0
```

执行过程为：

```text
rank 0 output 初始化 = rank 0 segment_0 = [0, 1]
rank 1 写入 rank 0   = rank 1 segment_0 = [100, 101]
rank 0 recvBuf       = [0, 1] + [100, 101] = [100, 102]

rank 1 output 初始化 = rank 1 segment_1 = [102, 103]
rank 0 写入 rank 1   = rank 0 segment_1 = [2, 3]
rank 1 recvBuf       = [102, 103] + [2, 3] = [104, 106]
```

最终输出：

```text
rank 0 recvBuf = [100, 102]
rank 1 recvBuf = [104, 106]
```

## 与 AllReduce 的区别

AllReduce 把完整输入归约到每个 rank，输入和输出元素数相同；ReduceScatter 只把第 `p` 段归约到
rank `p`，所以每个 rank 的输出元素数只有输入的 `1 / rankSize`。

## 与 MC2 样例的区别

`MC2/ascendc/ascendc_david_aicpu` 中的 `reduce_scatter_custom_v3` 是图算子/AscendC 路径：host 侧负责算子注册、
shape 推导和 tiling，AICore kernel 通过 `Hccl<HCCL_SERVER_TYPE_AICPU>::ReduceScatter` 提交通信，具体通信算法由
MC2/HCCL tiling（示例配置为 double ring）实现。

本样例是 CCU kernel 直发路径：host 侧直接申请 thread/channel、注册 CCU kernel，kernel 中显式编排本地拷贝、
地址交换、同步和 `WriteReduce`。它更适合学习或验证 CCU 数据面，但需要自行保证 offset、token、同步和资源生命周期正确。

## 目录结构

```text
custom_reduce_scatter/
|-- CMakeLists.txt
|-- README.md
|-- run.sh
|-- main.cc
|-- op_host/
|   |-- CMakeLists.txt
|   |-- alg_resource.cc
|   |-- alg_resource.h
|   |-- exec_op.cc
|   `-- exec_op.h
`-- op_kernel_ccu/
    |-- CMakeLists.txt
    |-- ccu_kernel.cc
    `-- ccu_kernel.h
```

## 构建和运行

执行前需要完成 asc-comm 数据面库构建或安装，并设置 CANN 与 CCU 调度环境：

```bash
source "${ASCEND_HOME_PATH}/set_env.sh"
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
bash run.sh
```

测试使用 `recvCount = 2`。rank `d` 的输入元素 `i` 为 `d * 100 + i`，程序会按 rank 检查两个输出值并打印
`PASS` 或 `FAIL`。这种数据布局会让不同 rank 得到不同结果，可以识别误写成 AllReduce 的实现。
