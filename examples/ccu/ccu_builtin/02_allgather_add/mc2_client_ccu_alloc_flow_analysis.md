# mc2_client CCU 内置 Server 方案设计

## 结论

这条链路是成立的，但边界要分清：

- `Mc2AcquireCcResCtxCcu` 只负责准备资源上下文
- `Mc2CcuCcKernel` 是 `__ccu_host__` 入口，走 `<<<>>>` 拉起
- `HcommCcuKernelRegisterStart/End` 不放进 `Mc2AcquireCcResCtxCcu`
- `stream` 只在 `Mc2CcuCcKernel<<<>>>` 的 launch 层传入

也就是说，新方案不是重写一套运行时，而是把现有 CCU 资源选择链和 launch 链拆开复用。

## 最终分层

### 1. Public API 层

对外声明放在 `include/adv_api/hccl/hccl_tiling.h`。

接口职责如下：

| 接口 | 作用 |
| --- | --- |
| `Mc2GetCcArgsCcu` | 分配一个 opaque 的 CCU 配置对象 |
| `Mc2FreeCcArgsCcu` | 释放配置对象 |
| `Mc2FreeCcResCtxCcu` | 释放资源上下文 |
| `Mc2SetCcCommEngineCcu` | 设置通信引擎 |
| `Mc2SetCcAlgConfigCcu` | 设置算法名或算法配置 |
| `Mc2SetCcSrcDataTypeCcu` | 设置源数据类型 |
| `Mc2SetCcDstDataTypeCcu` | 设置目标数据类型 |
| `Mc2SetCcReduceTypeCcu` | 设置 reduce 类型 |
| `Mc2AcquireCcResCtxCcu` | 根据 `HcclComm + ccArgs` 生成资源上下文和 launch 信息 |
| `Mc2CcuCcKernel` | `__ccu_host__` 内置 server 入口 |

`Mc2Result` 直接复用 `HcclResult`。

### 2. Host 资源准备层

`Mc2AcquireCcResCtxCcu` 复用现有能力，但不碰注册边界：

- `PrepareParamForAllGather`
- `PrepareParamForAllReduce`
- `HcclCalcTopoInfo`
- `ExecuteSelector`
- `PrepareEngineForAlg`
- `CalcAlgHierarchyInfo`
- `CalcRes`

这里生成的是：

- `ccResCtx`
- `ccResCtxSize`
- `schd`

其中 `ccResCtx` 是 opaque 的 builtin launch 上下文，内部保存资源请求和 kernel arg，但对外不开放解析。
`ccResCtx` 的所有权由调用方持有，`Mc2CcuCcKernel<<<>>>` 完成后应通过 `Mc2FreeCcResCtxCcu` 释放。

### 3. Launch 层

`Mc2CcuCcKernel<<<schd, insHandle, stream>>>` 走现有 `asccomm_ccu_host_kernel_launch` 体系。

这一层负责：

- 作为 builtin 入口消费 `ccResCtx`
- 遍历资源请求中的 `ccuKernelInfos`
- 直接分发到具体的 CCU kernel 入口

注册边界不在 `Mc2AcquireCcResCtxCcu`，也不再由 builtin 方案重复实现一套注册逻辑。

所以注册边界在 launch 层，不在 `Mc2AcquireCcResCtxCcu`。

### 4. 内置 Server 层

`Mc2CcuCcKernel` 作为 `__ccu_host__` 入口，内部只做 server dispatch：

- 解析 `ccResCtx`
- 进入内置 KFC dispatch core
- 复用现有 `CcuKfcServerKernel`

不要再给它单独造一套新的算法分发树。

## ABI 约束

### `ccArgs`

`ccArgs` 只保存公开配置，保持 opaque：

- `commEngine`
- `algConfig`
- `srcDataType`
- `dstDataType`
- `reduceType`
- 版本号和保留位

### `ccResCtx`

`ccResCtx` 只保存资源准备后的运行态上下文，不让样例直接解析内部字段。

### `schd`

`schd` 只保存 launch 所需的最小信息：

- `num_blocks`
- `phy_die_mask`
- `binary_cache_tag`

## 推荐调用链

```text
host sample
  -> Mc2GetCcArgsCcu
  -> Mc2SetCcCommEngineCcu
  -> Mc2SetCcAlgConfigCcu
  -> Mc2SetCcSrcDataTypeCcu
  -> Mc2SetCcDstDataTypeCcu
  -> Mc2SetCcReduceTypeCcu
  -> Mc2AcquireCcResCtxCcu
  -> Mc2FreeCcArgsCcu
  -> Mc2CcuCcKernel<<<schd, insHandle, stream>>>(ccResCtx)

device server
  -> Mc2CcuCcKernel
  -> 解析资源上下文
  -> 复用 KFC dispatch core
  -> 执行具体 CCU kernel
```

## 和现有实现的映射

| 现有实现 | 作用 | 新方案复用方式 |
| --- | --- | --- |
| `HcclAllocComResourceByTiling` | host 侧资源申请、算法选择、kernel 注册 | 复用其资源准备链，拆出注册边界 |
| `CcuKernelLaunch` | 恢复上下文并逐 mission launch | 复用其 launch 组织逻辑 |
| `HcommCcuKernelRegisterStart/End` | kernel 注册边界 | 由现有 launch 框架处理，不再放进 `Mc2AcquireCcResCtxCcu` |
| `HcommCcuKernelLaunch` | 最终下发动作 | 由现有 launch 框架处理 |
| `CcuKfcServerKernel` | server dispatch core | 作为 `Mc2CcuCcKernel` 的核心复用对象 |

## 关键约束

1. `Mc2AcquireCcResCtxCcu` 不调用 `HcommCcuKernelRegisterStart/End`
2. `Mc2CcuCcKernel` 必须是 `__ccu_host__` 入口
3. `ccArgs` 和 `ccResCtx` 都是 opaque handle
4. 单 die 场景优先，`phy_die_mask` 按实际资源结果填写
5. `Mc2CcuCcKernel` 只做入口和分发，不再复制一套算法实现

## 风险点

1. ABI 不稳定会直接导致 host/device 失配
2. 如果 `Mc2CcuCcKernel` 再分叉一套 dispatch，会造成双维护
3. `schd` 的 die mask 必须和实际资源结果一致
4. 后续扩展到更多 `ccType` 时，要提前保留版本字段
