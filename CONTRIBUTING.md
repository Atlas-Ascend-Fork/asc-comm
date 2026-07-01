# 贡献指南

本项目欢迎开发者参与 asc-comm 的代码、文档、样例和测试改进。参与贡献前，请先参考 [CANN 社区贡献指南](https://gitcode.com/cann/community) 了解行为准则、CLA 签署和社区通用 Issue/PR 流程。

## 贡献范围

- 问题修复：修复 AICore Hcomm API、构建脚本、UT 或文档中的缺陷。
- 兼容性完善：补充不同 CANN 版本、NPU 架构、Hcomm 协议路径的适配说明或测试。
- 文档与样例：完善 API 参考、使用指南、依赖说明和算子集成样例。
- 新能力支持：新增接口、协议路径或行为变更前，需要先通过 Issue 讨论方案。

## 开发流程

1. 查询已有 Issue，确认问题或需求是否已被跟踪。
2. 对简单修复或文档纠错，创建或关联 Issue 后即可提交 PR。
3. 对新增接口、配置参数、协议行为或较大流程调整，先提交 Requirement 类型 Issue，说明使用场景、业务价值、兼容性影响和初步方案。
4. 在本地创建独立分支完成修改，保持每个 PR 聚焦一个问题。
5. 提交 PR 后，根据检视意见更新代码、文档和测试，直到维护者确认可合入。

Issue 类型、填写字段和认领方式见 [Issue 提交流程](./docs/guide/issue_workflow.md)。

## 本地检查

提交前建议完成以下检查：

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh -t
```

若需要手动指定离线 GTest 路径，可参考 [三方依赖与兼容性](./docs/guide/dependencies.md) 使用 `CANN_3RD_LIB_PATH` 直接运行 CMake。

本仓使用 pre-commit 执行 C/C++ 格式化和 OAT 合规检查：

```bash
pip install pre-commit
pre-commit install
pre-commit run --all-files
```

检查项来自 `pre-commit-config.yaml`：`clang-format` 使用 v18.1.8，OAT 检查由 `scripts/oat_check.sh` 调用 `oat-py>=1.0.1`。

## 代码与文档要求

- C/C++ 代码遵循仓根目录 `clang-format` 配置。
- 公共 Hcomm 接口声明放在 `include/aicore/hcomm/`，平台相关实现放在 `include/aicore/hcomm/detail/impl/`。
- 修改 API 行为时，同步更新 `docs/api/`、`docs/guide/`、样例和 UT。
- 新增三方依赖时，同步更新 `Third_Party_Open_Source_Software_List.yaml`、Notice 和依赖文档。
- 涉及完整通信算子链路时，说明 asc-comm、hcomm control-plane、devkit/CANN 算子工程之间的边界。

## 提交与 PR 要求

提交信息保持简短、明确、单一主题，可使用中文描述或 `feat:`、`fix:`、`docs:` 等前缀，例如：

```text
docs: 补充 Hcomm 依赖说明
修复 UBC_CTP notify 文档描述
```

PR 描述应包含：

- 关联 Issue。
- 修改背景、目的和方案摘要。
- 已完成的验证命令和结果。
- 对 CANN 版本、NPU 架构、hcomm/devkit 外部依赖的假设。
- API、样例或文档行为变化说明。

不要提交 `build/`、`oat_reports/`、本地 CANN 安装目录、凭据、大型参考仓或其他生成产物。
