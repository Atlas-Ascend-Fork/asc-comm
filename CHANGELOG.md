# 更新日志

本文档记录asc-comm仓库的主要变更。

## Unreleased

### 新增

- 新增项目文档入口、快速开始、构建与测试、三方依赖与兼容性说明。
- 新增AICore Hcomm点对点通信接口API参考文档，覆盖`Hcomm`、`Init`、`ReadNbi`、`WriteNbi`、`WriteWithNotifyNbi`、`Commit`、`Drain`。
- 新增AICore Hcomm Kernel侧调用样例和完整算子集成边界说明。
- 新增Issue模板、贡献指南和资料贡献说明。

### 说明

- 当前公开能力主要覆盖从devkit迁移的AICore Hcomm点对点通信接口。
- 完整算子端到端运行仍依赖devkit/CANN算子工程能力和hcomm control-plane资源创建能力。
