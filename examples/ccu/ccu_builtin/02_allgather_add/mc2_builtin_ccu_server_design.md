# Mc2 内置 CCU Server 设计

## 结论

你的理解是对的。

现有 devkit 方案的核心链路是：

- `HcclAllocComResourceByTiling` 负责资源申请、算法分发前的准备、kernel 注册
- `CcuKernelLaunch` 负责把已经准备好的资源上下文真正 launch 到 `HcommCcuKernelLaunch`

opbase 的 `aclnn` 路径，本质上是调用这两类能力，再由框架把 CCU 任务打到 device 上。

你现在要做的新方案，目标不是重写一套 CCU 算法，而是：

- 保留现有 devkit 的资源申请和 launch 核心逻辑
- 通过新增 `Mc2GetCcArgsCcu` / `Mc2AcquireCcResCtxCcu` 等接口，替代 opbase 那条 `aclnn` 调用路径
- 最终用 `Mc2CcuCcKernel<<<>>>` 直接拉起内置的 CCU KFC server
- 用户侧不再手写 CCU kernel

这条方案是成立的，但前提是 `Mc2*` 接口不是空壳，它们必须把现有 `HcclAllocComResourceByTiling` 和 `CcuKernelLaunch` 里已经成熟的逻辑重新组织出来。



## 方案定位

### 现有两条路径

1. `ccu_direct/02_allgather_add`
   - 用户自己写 CCU kernel
   - 通过 `<<<>>>` 直接启动
   - 灵活，但侵入性高

2. devkit 高阶 HCCL API
   - 用户不写 CCU kernel
   - 框架内部完成资源申请、算法选择、launch
   - 更像完整产品路径

### 你要做的第三条路径

3. `Mc2* + <<<>>>`
   - 用户不写 CCU kernel
   - 通过 `Mc2*` 接口准备资源
   - 用 `Mc2CcuCcKernel<<<>>>` 直接启动内置 CCU server
   - 保留显式 launch 的控制感

## 现有能力拆解

### 1. `HcclAllocComResourceByTiling`

这个接口不是简单“分配内存”，它做的是一整套 host 侧资源准备：

- 校验 `comm / stream / mc2Tiling`
- 解析 `commEngine`
- 选择 AICPU 或 CCU 路径
- 进入 CCU 时做算法合法性检查
- 构造 `OpResCtx`
- 选择具体算法
- 申请 channel / thread / kernel 资源
- 注册 CCU kernel
- 序列化资源上下文到 device 可用内存

你可以把它理解成：

- **资源准备层**
- **算法选择层**
- **kernel 注册层**

三层合在一起的 host 实现。

其中最关键的可复用点是：

- `HcommCcuKernelRegisterStart`
- `HcommCcuKernelRegister`
- `HcommCcuKernelRegisterEnd`
- 资源上下文构造和序列化

### 2. `CcuKernelLaunch`

这个接口负责的是另一半：

- 从 device 侧 `opResCtx` 恢复 host 可读上下文
- 取出 `OpParam` / `AlgResourceCtxSerializable`
- 恢复 `threadHandle`、`kernelHandle`、`kfcServerArgs`
- 逐 mission 调 `HcommCcuKernelLaunch`

它本质上是：

- **launch 组织层**
- **上下文恢复层**
- **最终下发层**

你要复用的不是这个函数名字本身，而是它内部的“恢复上下文 + 组织 launch + 下发 kernel”的核心流程。

## 新方案目标

目标接口表面上长这样：

```cpp
Mc2GetCcArgsCcu(&ccArgs);
Mc2SetCcCommEngineCcu(ccArgs, CommEngine::COMM_ENGINE_CCU);
Mc2SetCcAlgConfigCcu(ccArgs, "fullmesh");
Mc2SetCcSrcDataTypeCcu(ccArgs, srcType);
Mc2SetCcDstDataTypeCcu(ccArgs, dstType);
Mc2SetCcReduceTypeCcu(ccArgs, reduceType);
Mc2AcquireCcResCtxCcu(comm, ccType, ccArgs, &ccResCtx, &ccResCtxSize, &schd);
Mc2FreeCcArgsCcu(ccArgs);
Mc2CcuCcKernel<<<schd, insHandle, streamCcu>>>(ccResCtx);
```

含义是：

- `ccArgs` 是用户输入配置
- `ccResCtx` 是 host 侧完成资源准备后，给 device server 消费的上下文
- `schd` 是 launch 所需的调度信息
- `Mc2CcuCcKernel` 是真正的 device entry

## 分层设计

### 1. API 层

职责：

- 对外提供最小接口
- 屏蔽资源申请、算法选择、kernel 注册细节

建议接口语义如下：

| 接口 | 作用 |
| --- | --- |
| `Mc2GetCcArgsCcu` | 分配并返回一个可配置的 CCU 参数对象 |
| `Mc2FreeCcArgsCcu` | 释放 `ccArgs` |
| `Mc2SetCcCommEngineCcu` | 设置通信引擎 |
| `Mc2SetCcAlgConfigCcu` | 设置算法名或算法配置 |
| `Mc2SetCcSrcDataTypeCcu` | 设置源数据类型 |
| `Mc2SetCcDstDataTypeCcu` | 设置目标数据类型 |
| `Mc2SetCcReduceTypeCcu` | 设置 reduce 类型 |
| `Mc2AcquireCcResCtxCcu` | 根据 `HcclComm + ccArgs` 生成设备侧资源上下文和 launch 信息 |
| `Mc2CcuCcKernel` | 作为 device kernel entry 被 `<<<>>>` 启动 |

### 2. Host 资源层

职责：

- 把 `ccArgs` 转换为内部可执行配置
- 读取 `HcclComm` 上的 rank、die、channel、topo 信息
- 复用现有算法选择和资源构造逻辑
- 生成 `ccResCtx`

这一层建议直接复用现有 devkit 中已经成熟的 helper，而不是重写算法：

- `BuildTagsAndValidate`
- `CcuSelectAlg`
- `AllocCcuOpResCtx`
- `RegisterCcuKernels`
- `BuildKfcLaunchCtx`

### 3. Device 服务端层

职责：

- 提供一个可被 `<<<>>>` 调起的内置 CCU server entry
- 接收 `ccResCtx`
- 还原 server 上下文
- 进入内置 dispatcher

这一层最适合做成“薄入口”：

- 只负责入口和上下文适配
- 不承载新的业务编排逻辑

### 4. 算法层

职责：

- 复用现有 CCU template / kernel 实现
- 不在样例侧重写通信细节

可以直接复用的方向包括：

- `CcuKfcServerKernel`
- `CcuKfcAllGatherMesh1DMem2MemKernel`
- 后续其他 CCU template kernel

### 5. AICore 计算层

职责：

- 在 CCU 完成 AllGather 后，做 Add 等 AICore 计算
- 这一层与 CCU server 解耦

## 推荐运行时链路

```text
Host Sample
  -> Mc2GetCcArgsCcu
  -> Mc2SetCcCommEngineCcu
  -> Mc2SetCcAlgConfigCcu
  -> Mc2SetCcSrcDataTypeCcu / Mc2SetCcDstDataTypeCcu / Mc2SetCcReduceTypeCcu
  -> Mc2AcquireCcResCtxCcu
  -> Mc2FreeCcArgsCcu
  -> Mc2CcuCcKernel<<<schd, insHandle, streamCcu>>>(ccResCtx)

Device Server
  -> 解析 ccResCtx
  -> 恢复 kernel / resource / mission 上下文
  -> 进入内置 dispatcher
  -> 分发到具体 CCU 算法
  -> 执行 AllGather

Host AICore
  -> 读取 AllGather 结果
  -> 执行 Add
```

## 接口定义建议

### 对外头文件落点

这套 `Mc2*` 接口建议**对外声明放在 `include/adv_api/hccl/hccl_tiling.h`**。

原因是：

- `hccl_tiling.h` 是现有对外发布头，样例和外部用户都能直接包含
- `hccl_mc2.h` 目前是内部头，不适合作为新公共 API 的入口
- 这样可以避免把新接口暴露在内部目录结构里，减少后续发布和安装包适配成本

实现层仍然可以保留在现有内部源码中，例如：

- 公共声明：`include/adv_api/hccl/hccl_tiling.h`
- 内部实现：`impl/adv_api/detail/hccl/cc/src/common/hccl_mc2.cc`
- 内部辅助声明：`include/adv_api/hccl/internal/hccl_mc2.h`

如果后续公共接口数量继续增长，再单独拆出 `hccl_mc2.h` 也可以，但当前阶段优先放进 `hccl_tiling.h` 更稳。

### `ccArgs`

建议是一个 host 侧的轻量配置对象，至少要包含：

- `version`
- `ccType`
- `commEngine`
- `algConfig`
- `srcDataType`
- `dstDataType`
- `reduceType`
- `reserved`

它可以是 opaque handle，也可以是带版本字段的轻量 struct，但要避免把 device 运行时状态直接暴露给用户。

### `ccResCtx`

建议是 device 侧的 opaque 上下文，包含：

- kernel 注册后句柄
- 资源上下文序列化数据
- rank / die / channel / mission 信息
- server 运行所需的 packed args

原则：

- 只允许 host 生成
- 只允许 device server 消费
- 不让样例逻辑直接解析内部字段

### `schd`

`schd` 只表达 launch 所需信息，不承载算法细节。

建议至少包含：

- block / grid 维度
- die mask
- 预留位

如果你后面要兼容现有 `asccomm_ccu_schd`，也可以继续沿用该形态，只要 ABI 固定即可。

## 与现有实现的映射关系

| 现有实现 | 作用 | 新方案中的复用方式 |
| --- | --- | --- |
| `HcclAllocComResourceByTiling` | host 侧资源申请、算法分发、kernel 注册 | 拆成 `Mc2AcquireCcResCtxCcu` 内部 helper |
| `CcuKernelLaunch` | 恢复上下文后逐 mission launch | 拆成 `Mc2CcuCcKernel` 入口可复用的 launch core |
| `HcommCcuKernelRegisterStart/End` | kernel 注册边界 | 继续放在 host 资源层 |
| `HcommCcuKernelLaunch` | 最终 device kernel 下发 | 继续作为底层 launch 动作 |
| `CcuKfcServerKernel` | device 侧内置 server dispatch | 作为 `Mc2CcuCcKernel` 的核心复用对象 |

## 关键复用点

### Host 侧

`Mc2AcquireCcResCtxCcu` 可以复用的核心逻辑：

- 通信域信息读取
- 拓扑和 channel 获取
- 算法选择
- kernel registration
- 资源序列化

### Device 侧

`Mc2CcuCcKernel` 可以复用的核心逻辑：

- server 端参数解析
- mission 循环
- kernel dispatch
- 具体 CCU template 调度

## 关键约束

1. `Mc2CcuCcKernel` 不是用户 kernel，而是内置 server entry
2. `ccArgs` 和 `ccResCtx` 必须版本化
3. `Mc2AcquireCcResCtxCcu` 必须在 launch 前就把运行态资源准备好
4. `Mc2CcuCcKernel` 只做入口和分发，不做业务重编排
5. 初期建议先只覆盖 `ALLGATHER + ADD` 这类单样例闭环场景

## 风险点

1. **ABI 风险**
   - `ccArgs` / `ccResCtx` 一旦字段不稳定，host 和 device 会失配

2. **重复实现风险**
   - 如果 `Mc2CcuCcKernel` 再写一套自己的 dispatch，很容易和 `CcuKfcServerKernel` 双维护

3. **接口漂移风险**
   - 如果 `Mc2*` 只做壳，实际逻辑仍然分散在其他地方，后续不好维护

4. **扩展风险**
   - 后续扩展到 `ALLREDUCE` / `REDUCE_SCATTER` 时，要提前预留 type 和 version

## 建议实现方式

建议把 `Mc2CcuCcKernel` 设计成**薄入口**：

- host 侧用 `Mc2AcquireCcResCtxCcu` 聚合现有资源申请逻辑
- device 侧复用 `CcuKfcServerKernel` 的 dispatch core
- `Mc2CcuCcKernel` 只负责适配新 ABI 和触发 server 流程

这样做的好处是：

- 用户侧接口简单
- 内部最大化复用现有 devkit 能力
- 后续算法扩展时，改动集中在 server/core 层

## 当前样例的落点

`examples/ccu/ccu_builtin/02_allgather_add/main.asc` 最终保留的形态建议是：

```cpp
Mc2GetCcArgsCcu(&ccArgs);
Mc2SetCcCommEngineCcu(ccArgs, CommEngine::COMM_ENGINE_CCU);
Mc2SetCcAlgConfigCcu(ccArgs, "fullmesh");
Mc2SetCcSrcDataTypeCcu(ccArgs, srcType);
Mc2SetCcDstDataTypeCcu(ccArgs, dstType);
Mc2SetCcReduceTypeCcu(ccArgs, reduceType);
Mc2AcquireCcResCtxCcu(comm, CC_TYPE_ALLGATHER, ccArgs, &ccResCtx, &ccResCtxSize, &schd);
Mc2FreeCcArgsCcu(ccArgs);
Mc2CcuCcKernel<<<schd, insHandle, streamCcu>>>(ccResCtx);
```

这样样例里就不会再出现用户自己写 CCU kernel 的部分。
