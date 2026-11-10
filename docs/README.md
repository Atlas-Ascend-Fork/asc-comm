# 项目文档

简体中文 | [English](./README_en.md)

## 目录说明

```text
docs/
├── zh/api/                    # 中文API参考文档
├── zh/experimental/           # CCU 2.0实验文档
├── zh/guide/                  # 中文使用指南
│   ├── index.md               # 使用指南入口
│   ├── communication_op_dev/  # 通信算子开发指南
│   └── programming_model/     # 编程模型
├── zh/compilation/            # 构建及三方依赖文档
├── zh/contributing/           # 贡献指南
├── en/api/                    # 英文API参考文档
├── en/guide/                  # 英文使用指南
├── en/compilation/            # 构建及三方依赖英文文档
├── en/contributing/           # 英文贡献指南
├── quick_start.md             # 快速开始
└── quick_start_en.md          # 快速开始英文版
```

> [!NOTE]说明
> 以下产品文档目录中的文档以中文版本为准。英文版本由专业团队翻译和审校后发布，受翻译与发布周期影响，可能滞后于中文版本。如中英文版本存在差异，请以中文版本为准。
>
> - [`./zh/api`](./zh/api/)
> - [`./zh/guide`](./zh/guide/)
> - [`./en/api`（译自`./zh/api`）](./en/api/)
> - [`./en/guide`（译自`./zh/guide`）](./en/guide/)

## 文档入口

| 文档 | 内容 |
| --- | --- |
| [快速开始](./quick_start.md) | asc-comm环境准备、源码编译和UT验证。 |
| [通信编程](./zh/guide/index.md) | 编程模型与通信算子开发指南入口。 |
| [API参考](./zh/api/index.md) | asc-comm当前公开接口列表。 |
| [CCU实验文档](./zh/experimental/README.md) | CCU 2.0 kernel的编写、编译和运行说明。 |
| [构建与测试](./zh/compilation/build_and_test.md) | CANN环境、开发验证run包、UT构建和样例构建说明。 |
| [三方依赖与兼容性](./zh/compilation/dependencies.md) | 本仓直接依赖、样例运行依赖、安装配置和集成依赖边界。 |
| [API文档贡献指南](./zh/contributing/api_contributing.md) | 新增或修改API文档时的结构、约束和检查要求。 |
| [资料贡献指南](./zh/contributing/doc_contributing.md) | README、docs、examples等资料文档的补充规范。 |
| [贡献指南](../CONTRIBUTING.md) | Issue、开发、检查和PR提交流程。 |
| [样例](../examples/README.md) | asc-comm API使用样例入口，包含AIV直驱URMA WriteNbi/ReadNbi点对点通信样例。 |
