# Mc2 内置 CCU AllGather + Add 方案分析

## 结论

这个方案**可行，但前提是 `Mc2*` 接口要补齐完整实现**，而且不只是 host 侧函数实现，还要有对应的 device 侧 kernel entry。

你现在这版思路，本质上是把原来 `ccu_direct` 里“样例自己拼参数、自己调 CCU kernel”的方式，改成：

1. host 侧只准备抽象参数
2. 由 `Mc2AcquireCcResCtxCcu` 统一生成资源上下文
3. 通过 `Mc2CcuCcKernel<<<>>>` 启动内置的 CCU 服务端
4. 服务端再分发到具体的内置 CCU 算法 kernel

这条链路在架构上是通的，但当前仓里只有声明，没有实现，所以还不能直接跑。

## 现状

目前 `asc-devkit-bluesky901/include/adv_api/hccl/internal/hccl_mc2.h` 和 `hccl_tiling.h` 里只有这些声明：

- `Mc2GetCcArgsCcu`
- `Mc2FreeCcArgsCcu`
- `Mc2SetCcCommEngineCcu`
- `Mc2SetCcAlgConfigCcu`
- `Mc2SetCcSrcDataTypeCcu`
- `Mc2SetCcDstDataTypeCcu`
- `Mc2SetCcReduceTypeCcu`
- `Mc2AcquireCcResCtxCcu`
- `Mc2CcuCcKernel`

也就是说，样例里这段流程现在只是设计草图：

```cpp
Mc2GetCcArgsCcu(&ccArgs);
Mc2SetCcCommEngineCcu(ccArgs, CommEngine::COMM_ENGINE_CCU);
Mc2SetCcAlgConfigCcu(ccArgs, "fullmesh");
Mc2AcquireCcResCtxCcu(comm, ccType, ccArgs, &ccResCtx, &ccResCtxSize, &schd);
Mc2FreeCcArgsCcu(ccArgs);
Mc2CcuCcKernel<<<schd, insHandle, streamCcu>>>(ccResCtx);
```

## 方案判断

### 可以做的部分

`Mc2*` 这套接口可以作为一层更薄的 host API，把以下事情收拢起来：

- 创建和释放 `ccArgs`
- 写入 `commEngine` / `algConfig` / `dtype` / `reduceType`
- 根据 `HcclComm` 和 `ccArgs` 组装出 `ccResCtx`
- 生成 CCU 启动所需的调度信息 `schd`

### 必须补的部分

要让 `Mc2CcuCcKernel<<<>>>` 真正可跑，还需要：

1. 定义 `ccArgs` 和 `ccResCtx` 的稳定二进制布局
2. 实现 `Mc2*` host 侧函数
3. 在 `libmc2_server.so` 里增加对应的 device kernel entry
4. 在 `mc2_server.ini` 里注册 `Mc2CcuCcKernel`
5. 让 device 侧 entry 把 `ccResCtx` 解析后分发到具体 CCU 算法

## 推荐实现路径

我建议不要把 `Mc2CcuCcKernel` 做成“全新完全独立的一套运行时”，而是做成**薄封装**：

- host 侧复用现有 `HcclAllocComResourceByTiling` 里已经成熟的资源选择逻辑
- `Mc2AcquireCcResCtxCcu` 只负责把 `ccArgs` 映射成内部可识别的上下文
- device 侧 `Mc2CcuCcKernel` 复用现有 `Mc2ServerKernel` 的调度框架

这样做的好处是：

- 样例侧接口更干净
- 不需要在样例里重复写 selector / channel / kernelArg 组装逻辑
- 能最大化复用现有 `mc2_client` / `mc2_server` 的能力

## 伪流程

```text
host:
  Mc2GetCcArgsCcu
  -> Mc2SetCcCommEngineCcu
  -> Mc2SetCcAlgConfigCcu
  -> Mc2SetCcSrcDataTypeCcu / Mc2SetCcDstDataTypeCcu
  -> Mc2AcquireCcResCtxCcu
  -> Mc2FreeCcArgsCcu
  -> Mc2CcuCcKernel<<<>>> (ccResCtx)

device:
  Mc2CcuCcKernel
  -> 解析 ccResCtx
  -> 加载/恢复通信资源
  -> 分发到具体内置 CCU kernel
  -> 执行 AllGather
  -> 由 AICore side 做 Add
```

## 风险点

1. `Mc2CcuCcKernel` 的 ABI 必须固定，否则 host/device 结构体不兼容会直接跑飞。
2. 如果 `Mc2AcquireCcResCtxCcu` 只是简单拼壳，但 device 侧没有对应解析逻辑，这个方案落不下来。
3. 如果你希望它“完全不依赖 nnopbase”，那就不能复用 nnopbase 那层动态 wrapper，必须在 `libmc2_server.so` 里直接打通入口。
4. `ccType = ALLGATHER / ALLREDUCE` 这种枚举最好尽早定死，否则后续扩展会反复改 ABI。

## 结论

这条方案的核心不是“能不能用 `<<<>>>` 拉起”，而是**能不能把 host 侧参数、device 侧 entry、内置算法分发三部分对齐**。

如果这三块都补齐，方案是成立的；如果只补 `Mc2*` 声明对应的 host API，而不补 device entry，就只能停留在伪码阶段。

