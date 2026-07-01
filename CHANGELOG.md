# 更新日志

本文档记录 asc-comm 仓库的主要变更。

## Unreleased

### 新增

- 新增项目文档入口、快速开始、构建与测试、三方依赖与兼容性说明。
- 新增 AICore Hcomm 点对点通信接口 API 参考文档，覆盖 `Hcomm`、`Init`、`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`Commit`、`Drain`。
- 新增 AICore Hcomm Kernel 侧调用样例和完整算子集成边界说明。
- 新增 Issue 模板、贡献指南和资料贡献说明。

### 说明

- 当前公开能力主要覆盖从 devkit 迁移的 AICore Hcomm 点对点通信接口。
- 完整算子端到端运行仍依赖 devkit/CANN 算子工程能力和 hcomm control-plane 资源创建能力。
