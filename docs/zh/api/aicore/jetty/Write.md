# Write

## 功能说明

向Jetty SQ提交一个URMA写WQE。SIMD路径提交单WQE写任务；SIMT路径支持thread/warp/group三种协作粒度构造WQE，单个WQE最多携带12个SGE。

## 函数原型

SIMD路径：

```cpp
template <int64_t timeoutCycles, bool doCommit = true>
__aicore__ inline int32_t Write(
    const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes);
```

SIMT路径：

```cpp
template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
__simt_callee__ __forceinline__ int32_t Write(
    const HcommPeer& peer, __gm__ void* dst, const JettySge* sges, const Coop& coop = Coop{}) const;
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `peer` | 输入 | `HcommPeer`解析的远端信息。 |
| `dstAddr` / `dst` | 输出 | 远端目的GM绝对地址。SIMT路径由各SGE指定。 |
| `srcAddr` | 输入 | 本端源GM绝对地址（SIMD路径）。 |
| `numBytes` | 输入 | 写入长度，单位为字节（SIMD路径）。 |
| `sges` | 输入 | SGE数组，每个SGE携带一段本端源地址与长度（SIMT路径）。 |
| `coop` | 输入 | 协作组对象，thread/warp/group（SIMT路径）。 |

## 模板参数

| 参数 | 说明 |
| --- | --- |
| `timeoutCycles` | 轮询CQE时的等待周期上限（SIMD路径）。 |
| `doCommit` / `kDoCommit` | 是否在提交时立即发布。设为`false`时任务进入延迟提交路径。 |
| `kSgeNum` | WQE携带的SGE数量，范围1到12（SIMT路径）。 |
| `Coop` | 协作组类型，默认`HcommCoopThread`（SIMT路径）。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | WQE提交成功。 |
| `-1` | 操作失败，包括SQ空间不足。 |

## 约束说明

- 仅支持Ascend 950平台的`COMM_PROTOCOL_UB_CTP`路径。
- `dst`需要落在通道注册的远端buffer范围内。
- SIMT路径下，地址或长度为0的SGE不计入WRITE，有效SGE自动压紧。
- `kDoCommit = false`时必须随后调用`AdvanceSq`和`PublishSq`完成发布。
- warp协作构造时，每个lane通过shuffle贡献一个SGE，warp内SGE顺序与lane序号一致。
