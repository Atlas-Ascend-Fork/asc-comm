# ain

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

头文件路径：`"ain/ain.h"`。

`asc::ain`是AI Core侧单边通信接口模板，提供`put`/`get`/`signal`/`flush`等单边通信及同步能力。`ain`会根据通信组team、对端rank和对称window句柄解析通信通道及通信内存地址，Kernel侧无需直接维护远端地址。其中，team为Host侧创建的通信组，rank为通信域内的节点编号，对称window为通信各rank按相同地址和大小注册的对称内存窗口，单边任务基于window句柄和偏移访问对端内存。

## 函数原型

```cpp
template <unsigned comm_engine_mask = AIN_MASK_DEFAULT>
class ain;
```

构造函数：

```cpp
__aicore__ inline ain(uint32_t context_index = 0);
```

## 参数说明

**表1** 模板参数说明

| 参数名 | 描述 |
| --- | --- |
| `comm_engine_mask` | 通信引擎选择掩码，默认值为`AIN_MASK_DEFAULT`，对应通信引擎为AIV（AI Vector，向量核）。目前仅支持AIV，暂不支持其他通信引擎。 |

**表2** 接口参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| `context_index` | 输入 | 通信上下文索引，默认值为0。 |

## 约束说明

- 调用前Host侧需完成team创建、对称window注册。
- 传入的`HcommTeamHandle`和`HcclCommSymWindow`需要是有效的device侧句柄。
- 当前只支持`COMM_PROTOCOL_UB_CTP`协议。
- [put](./put.md)、[put_value](./put_value.md)、[get](./get.md)和[signal](./signal.md)内部会初始化底层asccomm组件（点对点通信引擎，参见[asccomm](../asccomm/asccomm.md)），调用时需要提供有效的`ain_descriptor_ubuf`。
- 使用`AIN_COMMIT_DELAYED`时，连续积攒的任务槽位不得超过通道的任务提交容量（即底层SQ（Send Queue，发送队列）深度sqDepth）；需在容量耗尽前通过`AIN_COMMIT_IMMED`提交积攒的任务，否则后续任务将因SQ溢出而失败。`put`/`put_value`/`get`各占1个任务槽位，其中`put`/`put_value`配置`ain_signal_inc`/`ain_signal_add`远端动作时额外追加2个任务槽位（共3个）；`signal`占2个任务槽位。
- 批量提交场景（多次`AIN_COMMIT_DELAYED` + 最后一次`AIN_COMMIT_IMMED`）下，仅最后一次提交应产生CQE（Completion Queue Entry，完成队列项），需将中间任务的`config`中`cqe`字段设为0、最后一次任务设为1；默认`URMA_DEFAULT_CFG`/`URMA_INLINE_CFG`（URMA，Unified Remote Memory Access，统一远端内存访问）的`cqe`为1，未调整配置时每个任务均会产生CQE。

## 调用示例

Ain接口的调用示例如下，完整样例参见[01_basic_ring](../../../../../examples/aicore/ain/01_basic_ring/README.md)。

```cpp
// 单次通信数据传输大小
constexpr uint64_t BYTES = 1024U;
// 初始化UBuf（Unified Buffer，统一缓冲区）工作区大小：不得小于512B
constexpr uint32_t ASCCOMM_BUF_SIZE = 512U;

extern "C" __vector__ __global__ __aicore__ void ain_put_kernel(
    GM_ADDR team, GM_ADDR dst_win, GM_ADDR src_win,
    uint32_t rank_num, uint32_t rank_id)
{
    // 准备UBuf工作区；alignas(32)保证起始地址按32字节对齐
    alignas(32) __ubuf__ uint8_t asccomm_local[ASCCOMM_BUF_SIZE];
    ain_descriptor_ubuf ubuf{asccomm_local, ASCCOMM_BUF_SIZE, 0U};
    // 创建Ain对象
    asc::ain ain;
    // 对端rank_id
    uint32_t peer = (rank_id + 1) % rank_num;
    // 单边写：本端srcWin数据写入对端dstWin
    ain.put(team, peer, dst_win, rank_id * BYTES, src_win, 0U, BYTES,
            ain_remote_none{}, ubuf);
    // 等待team内所有peer通道通信任务完成
    ain.flush(team);
}
```
