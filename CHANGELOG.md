# 更新日志

本文档记录asc-comm仓库的主要变更。

## Unreleased

### 新增

- 新增项目文档入口、快速开始、构建与测试、三方依赖与兼容性说明。
- 新增AICore asccomm点对点通信接口API参考文档，覆盖`asccomm`、`init`、`read_nbi`、`write_nbi`、`write_with_notify_nbi`、`atomic_faa`、`atomic_cas`、`commit`、`drain`。
- 新增AIV直驱URMA asccomm `hcomm_write_read_nbi`样例，覆盖`write_nbi`和`read_nbi`两卡点对点通信流程。
- 新增Issue模板、贡献指南和资料贡献说明。

### 说明

- 当前公开能力主要覆盖AICore asccomm点对点通信接口。
