# 编程模型文档质量审查与问题跟踪

## 1. 审查说明

本文记录 `docs/zh/编程模型` 下文档的质量审查结果，供后续修改、评审和关闭问题时跟踪。

- 审查日期：2026-08-12
- 审查范围：
  - `proxy_model_overview.md`
  - `proxy_comm_model.md`
  - `proxy_exec_model.md`
  - `memory_model.md`
  - 上述文档引用的图片
- 对照范围：`asc-comm`、`asc-devkit`、`hcomm` 的公开接口、实现、测试和 API 文档
- 状态取值：`待修改`、`待确认`、`修改中`、`暂缓`、`已解决`、`不修改`

本文区分以下两类问题：

- **确定的文档问题**：现有证据足以确认，可以直接修改文档。
- **跨仓一致性问题**：文档、接口约束和代码实现之间存在冲突，需要责任人先确定正确契约，再决定修改哪一侧。

## 2. 结论摘要

这组文档已经形成“概述、通信模型、执行模型、内存模型”的基本结构，主要概念和图示整体可读，AICPU_TS 与 CCU 两种代理形态的描述也有代码实现支撑。`Wait` 完成语义及多轮、细粒度任务约束已经补充；当前主要风险为跨仓契约冲突和产品接口支持边界。直驱模式断链、公共概念和文档入口暂缓到后续文档完善阶段处理。

### 2.1 文档质量评级

| 文档 | 正确性 | 完整性 | 可读性 | 可维护性 | 综合评价 | 发布建议 |
| --- | --- | --- | --- | --- | --- | --- |
| `proxy_model_overview.md` | 良好 | 一般 | 良好 | 一般 | 7.5/10 | 当前内容可发布，文档入口后续完善 |
| `proxy_comm_model.md` | 良好 | 一般 | 良好 | 一般 | 7.0/10 | 代理模型内容可发布，直驱文档和公共概念后续补充 |
| `proxy_exec_model.md` | 良好 | 良好 | 良好 | 一般 | 7.5/10 | 确认 Finalize 契约并补充接口支持边界后发布 |
| `memory_model.md` | 待确认 | 良好 | 良好 | 一般 | 6.0/10 | Token 契约统一后发布 |

### 2.2 当前状态统计

| 类型 | 待修改 | 待确认 | 暂缓 | 已解决 | 合计 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 确定的文档问题 | 2 | 0 | 3 | 2 | 7 |
| 跨仓一致性问题 | 0 | 2 | 0 | 0 | 2 |

## 3. 已确认的实现事实

以下事实作为本轮审查的前提，后续修改不应再次使用过期 API 文档推翻这些结论。

### 3.1 Ascend 950 支持 AICPU_TS 代理服务端

Ascend 950 的高阶 HCCL 客户端同时包含 AICPU 和 CCU 两套实现：

- `asc-devkit/impl/adv_api/detail/hccl/impl/hccl_impl_def.h:24` 在 `__NPU_ARCH__ == 3510` 时加载 v310 实现。
- `asc-devkit/impl/adv_api/detail/hccl/impl/hccl_v310_impl.h:18` 同时包含 AICPU 和 CCU 的实现定义。
- `asc-devkit/impl/adv_api/detail/hccl/impl/platform_v310/hccl_aicpu.h:30` 实现 AICPU 模式的 `InitV2`，第 48 行实现 `SetCcTilingV2`。
- `asc-devkit/impl/adv_api/detail/hccl/common/hccl_aicpu_impl.h` 实现集合通信 Prepare、`Commit`、`Wait` 和 `Finalize` 流程。
- `asc-devkit/tests/api/adv_api/hccl/test_hccl_aicpu_950.cpp` 包含 950 AICPU 模式的专用测试，并由 `tests/api/adv_api/CMakeLists.txt:458` 纳入 950 测试目标。

因此，`proxy_model_overview.md`、`proxy_comm_model.md` 和 `proxy_exec_model.md` 将 AICPU_TS 与 CCU 并列为 Ascend 950 的代理服务端是正确的。

`asc-devkit/docs/zh/api/SIMD-API/adv_api/HCCL_communication/HCCL_Kernel/HCCL_template_params.md:45` 中“Ascend 950 当前仅支持 CCU”的描述已经落后于实现，应在 asc-devkit 中单独更新，不能作为修改本目录文档的依据。

### 3.2 AICPU 模式并非支持所有高阶接口

950 AICPU 模式已经覆盖主要集合通信流程，但仍存在未实现接口：

- `BatchWrite` 和 `AlltoAllvWrite` 返回 `-1`。
- `Iterate` 返回 `HCCL_FAILED`。
- `InterHcclGroupSync`、`QueueBarrier` 等接口当前为空实现。

编程模型可以描述 AICPU_TS 代理形态，但涉及具体 API 支持范围时，应链接到更新后的产品支持表，避免表述成所有高阶接口均可用。

## 4. 确定的文档问题

### DOC-P1-001：直驱模式链接失效

| 字段 | 内容 |
| --- | --- |
| 状态 | 暂缓 |
| 责任仓 | asc-comm |
| 位置 | `proxy_comm_model.md:3` |
| 影响 | 用户点击“直驱模式通信模型”后无法打开目标文档；仓库链接检查失败 |

`proxy_comm_model.md` 引用了尚未实现的 `direct_comm_model.md`。直驱模式文档计划后续补充，当前保留链接并暂缓处理，因此仓库链接检查会继续报告此项失败。

后续处理：

1. 完成并提交 `direct_comm_model.md`。
2. 修改后执行：

```bash
MARKDOWN_LINK_CHECK_ONLINE=0 bash scripts/markdown_link_check.sh \
  docs/zh/编程模型/proxy_model_overview.md \
  docs/zh/编程模型/proxy_comm_model.md \
  docs/zh/编程模型/proxy_exec_model.md \
  docs/zh/编程模型/memory_model.md
```

### DOC-P1-002：`Wait` 返回与成功完成被错误等同

| 字段 | 内容 |
| --- | --- |
| 状态 | 已解决 |
| 责任仓 | asc-comm |
| 位置 | `proxy_exec_model.md:41`、`proxy_exec_model.md:75`、`proxy_exec_model.md:89`、`proxy_exec_model.md:91` |
| 影响 | `Wait` 失败时，用户可能仍读取或复用尚未完成的通信内存 |

原文将“`Wait` 返回”直接表述为任务已经完成并允许读取输出，但 `Wait` 返回 `int32_t`，可能返回 `HCCL_FAILED`。asc-devkit 的 CCU 和 AICPU 实现也都存在失败返回路径。

处理结果：`proxy_exec_model.md` 已统一明确，只有 `Wait(handleId)` 返回 `HCCL_SUCCESS`，才能认为对应任务完成并读取输出或复用相关内存；返回 `HCCL_FAILED` 时不表示任务完成，需要按接口错误处理要求退出当前流程。

### DOC-P2-001：未说明 `repeat` 和细粒度通信的 Commit/Wait 关系

| 字段 | 内容 |
| --- | --- |
| 状态 | 已解决 |
| 责任仓 | asc-comm |
| 位置 | `proxy_exec_model.md:50`、`proxy_exec_model.md:64`、`proxy_exec_model.md:71`、`proxy_exec_model.md:77` |
| 影响 | 多轮任务可能少调用或多调用 `Commit`/`Wait`，导致任务未发布、错误返回或资源无法复用 |

一次 Prepare 调用只产生一个 `handleId`，但该 handle 可以通过 `repeat` 表示多轮任务。非细粒度场景下，显式 `Commit` 和 `Wait` 的调用次数应与 `repeat` 一致；细粒度场景还需要考虑任务总步骤数与 `stepSize`。多个 handle 的 `Wait` 顺序也必须与 Prepare 顺序一致。

处理结果：`proxy_exec_model.md` 已新增“多轮与细粒度任务”小节，说明普通通信和细粒度通信的 `Commit`/`Wait` 次数、`commit = true` 和 `Iterate<true>` 的行为，以及多个 handle 的等待顺序；图 2 已明确标注为非细粒度、`repeat = 1` 的成功路径。

### DOC-P2-002：通信模型缺少 Channel、Endpoint 和 Notify 的基本定义

| 字段 | 内容 |
| --- | --- |
| 状态 | 暂缓 |
| 责任仓 | asc-comm |
| 位置 | `proxy_comm_model.md` 全文，尤其是第 9 行和第 51 行 |
| 影响 | 文档直接使用核心概念，但读者无法从当前目录获得其定义、基数关系和作用边界 |

Channel、Endpoint、Notify、通信实体等公共概念计划在后续直驱模式文档中统一说明。当前 `proxy_model_overview.md:19` 仍承诺通信模型会说明这些对象的组成关系，但 `proxy_comm_model.md` 主要描述任务代理流程，没有解释：

- 通信实体与 rank 的关系；
- Endpoint 表示什么；
- Channel 如何选择对端、Endpoint、协议和远端内存；
- Notify/Variable 属于 Channel 资源还是代理任务介质；
- 代理介质与 Channel 数据通路的边界。

后续处理：完成直驱模式文档后，从代理通信模型链接到其中的公共概念定义。本轮不在代理通信模型中重复补充。

### DOC-P2-003：未明确 AICPU 模式的接口支持边界

| 字段 | 内容 |
| --- | --- |
| 状态 | 待修改 |
| 责任仓 | asc-comm、asc-devkit |
| 位置 | `proxy_exec_model.md:16`、`proxy_exec_model.md:48` |
| 影响 | 读者可能将统一生命周期误解为两类服务端支持完全相同的高阶接口集合 |

两类服务端都支持 Prepare、`Commit`、`Wait`、`Finalize` 生命周期，不等于所有 Prepare 原语和辅助接口都具有相同支持范围。950 AICPU 实现中仍有明确返回失败或空实现的接口。

建议在执行模型中增加一句边界说明：服务端和产品的具体原语支持范围以 API 产品支持表为准。同时更新 asc-devkit 中已经过期的产品支持表。

### DOC-P3-001：编程模型没有文档入口

| 字段 | 内容 |
| --- | --- |
| 状态 | 暂缓 |
| 责任仓 | asc-comm |
| 位置 | `docs/README.md` |
| 影响 | 4 篇正文只能通过路径发现，无法从仓库文档入口正常导航进入 |

后续完善文档导航时，在 `docs/README.md` 的“目录说明”和“文档入口”中加入“编程模型”，入口指向 `zh/编程模型/proxy_model_overview.md`。本轮暂缓处理。

### DOC-P3-002：部分图片缺少替代文本

| 字段 | 内容 |
| --- | --- |
| 状态 | 待修改 |
| 责任仓 | asc-comm |
| 位置 | `proxy_comm_model.md:17`、`proxy_comm_model.md:32`、`proxy_exec_model.md:22`、`proxy_exec_model.md:52` |
| 影响 | 无障碍阅读、纯文本渲染和图片加载失败时缺少语义信息 |

建议将空的 `![](...)` 替换为带有简洁替代文本的图片引用，例如 `![AICPU_TS 代理模式通信模型](...)`。

## 5. 跨仓一致性问题

### CONS-P0-001：CCU Token 的跨 rank 交换契约冲突

| 字段 | 内容 |
| --- | --- |
| 状态 | 待确认 |
| 责任仓 | hcomm、asc-comm |
| 位置 | `memory_model.md:141`、`memory_model.md:151` |
| 影响 | 涉及访问凭据的安全边界，不能在契约未统一时直接发布指导流程 |

当前存在两组相互冲突的证据：

1. `hcomm/include/ccu/ccu_res.h:124` 和 `HcommCcuGetMemToken.md:60` 要求 Token 通过带外通道交换，禁止通过 CCU 数据面明文传输。
2. `hcomm/docs/zh/comm_op_dev_guide/ccu_comm_op_dev/algo_exec.md:108` 的示例，以及 `memory_model.md:151`，都使用 `WriteVariableWithNotify` 通过 Channel 交换 Token。

这说明问题不只存在于 asc-comm，hcomm 的接口注释、API 文档和开发指南本身也不一致。不能仅凭其中一侧判断 `memory_model.md` 正确或错误。

待确认问题：

- `WriteVariableWithNotify` 传递 Token 是否属于允许的受保护数据通路，而“禁止 CCU 数据面明文传输”仅禁止其他形式？
- 如果允许，需要修改 `ccu_res.h` 和 `HcommCcuGetMemToken.md`，准确说明允许条件和保护机制。
- 如果不允许，需要修改 hcomm 开发指南和 `memory_model.md`，改为 Host/控制面预交换 Token，并通过 Kernel 参数下发。

关闭标准：hcomm 的公开头文件、API 文档、开发指南以及本目录内存模型对 Token 流转给出同一套可执行规则。

### CONS-P1-001：CCU `Finalize<false>` 文档契约与实现不一致

| 字段 | 内容 |
| --- | --- |
| 状态 | 待确认 |
| 责任仓 | asc-devkit、asc-comm |
| 位置 | `proxy_exec_model.md:85`、`proxy_exec_model.md:86` |
| 影响 | 用户无法确定 `Finalize<false>` 在 CCU 模式下是否真正异步 |

文档契约规定：

- `Finalize<true>` 等待最后一个通信任务完成；
- `Finalize<false>` 只发送结束通知，不等待已下发任务完成。

但 `asc-devkit/impl/adv_api/detail/hccl/impl/platform_v310/hccl_ccu_v0.h:570` 的 CCU 实现没有使用模板参数 `sync`，并在第 579 行无条件等待已提交任务完成。AICPU 实现则通过 `if constexpr (sync)` 区分两种语义。

待确认问题：

- 如果接口契约正确，应修复 CCU 实现并补充 `<false>` 测试。
- 如果 CCU 当前不支持异步 Finalize，应在 API 产品支持表和编程模型中明确限制，不能继续给出统一语义。

关闭标准：接口文档、AICPU/CCU 实现和测试对 `sync` 参数的行为一致。

## 6. 非问题与撤销项

### CLOSED-001：Ascend 950 是否支持 AICPU_TS 代理

| 字段 | 内容 |
| --- | --- |
| 状态 | 已解决 |
| 结论 | 支持；原“仅支持 CCU”的审查结论撤销 |

代码实现和专用测试均证明 950 已支持 AICPU_TS 代理。问题位于 asc-devkit 的旧 API 产品支持说明，而不在本目录对两种服务端的模型描述。

## 7. 建议修改顺序

1. 由 hcomm 责任人确定 Token 的合法交换路径，关闭 `CONS-P0-001`。
2. 由 asc-devkit 责任人确定并统一 CCU `Finalize<false>` 契约，关闭 `CONS-P1-001`。
3. 补充 AICPU/CCU 的具体接口支持边界说明，同时更新 asc-devkit 过期产品支持表。
4. 补充图片替代文本。
5. 后续完成直驱模式文档，关闭断链并提供 Channel、Endpoint、Notify 基本定义。
6. 后续完善 `docs/README.md` 中的编程模型入口，并执行链接检查。

## 8. 验收清单

- [ ] 所有 P0/P1 问题均已关闭。
- [x] `Wait` 相关表述只在返回 `HCCL_SUCCESS` 时承诺数据可用。
- [x] 文档覆盖 `repeat > 1` 和细粒度通信的 Commit/Wait 次数与顺序。
- [ ] AICPU_TS 与 CCU 的产品和接口支持边界与实现一致。
- [ ] Token 流转规则在 hcomm 头文件、API 文档、开发指南和本内存模型中一致。
- [ ] `Finalize<false>` 的文档、实现和测试一致。
- [ ] `direct_comm_model.md` 不再形成断链。
- [ ] `docs/README.md` 提供编程模型入口。
- [ ] 所有模型图片都有有效替代文本。
- [ ] 仓库 Markdown 链接检查通过。

## 9. 跟踪记录

| 日期 | 修改人 | 问题 ID | 状态变化 | 说明 |
| --- | --- | --- | --- | --- |
| 2026-08-12 | Codex | 全部 | 初始记录 | 完成跨 asc-comm、asc-devkit、hcomm 的代码与文档对照审查 |
| 2026-08-12 | Codex | CLOSED-001 | 待确认 -> 已解决 | 根据 v310 AICPU 实现和 950 专用测试，撤销“不支持 AICPU_TS”的结论 |
| 2026-08-12 | Codex | DOC-P1-001 | 待修改 -> 暂缓 | 直驱模式文档尚未实现，保留链接并在后续补充目标文档 |
| 2026-08-12 | Codex | DOC-P1-002 | 待修改 -> 已解决 | 明确仅 `Wait` 返回 `HCCL_SUCCESS` 才表示任务完成和输出可用 |
| 2026-08-12 | Codex | DOC-P2-001 | 待修改 -> 已解决 | 补充 `repeat`、细粒度任务、自动提交和等待顺序规则 |
| 2026-08-12 | Codex | DOC-P2-002 | 待修改 -> 暂缓 | Channel、Endpoint、Notify 定义随直驱模式文档后续补充 |
| 2026-08-12 | Codex | DOC-P3-001 | 待修改 -> 暂缓 | 文档入口随文档体系后续完善 |
