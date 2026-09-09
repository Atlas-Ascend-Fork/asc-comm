# 项目文档

简体中文 | [English](./README_en.md)

## 目录说明

```text
docs/
├── zh/api/              # 中文API参考文档
├── zh/guide/            # 中文使用、构建与测试指南
├── zh/programming_model/ # 中文编程模型文档
├── en/api/              # 英文API参考文档
├── en/guide/            # 英文使用、构建与测试指南
├── api_contributing.md  # API文档贡献指南
├── doc_contributing.md  # 资料贡献指南
└── quick_start.md       # 快速开始
```

## 文档入口

| 文档 | 内容 |
| --- | --- |
| [快速开始](./quick_start.md) | asc-comm环境准备、源码编译和UT验证。 |
| [编程模型](./zh/programming_model/overview.md) | Ascend C通信编程范式、抽象硬件架构及通信方式的分类与选择。 |
| [API参考](./zh/api/README.md) | asc-comm当前公开接口列表。 |
| [Hcomm使用说明](./zh/guide/hcomm_usage.md) | Hcomm点对点通信接口的基本使用流程。 |
| [构建与测试](./zh/guide/build_and_test.md) | CANN环境、开发验证run包、UT构建和样例构建说明。 |
| [三方依赖与兼容性](./zh/guide/dependencies.md) | 本仓直接依赖、样例运行依赖、安装配置和集成依赖边界。 |
| [API文档贡献指南](./api_contributing.md) | 新增或修改API文档时的结构、约束和检查要求。 |
| [资料贡献指南](./doc_contributing.md) | README、docs、examples等资料文档的补充规范。 |
| [贡献指南](../CONTRIBUTING.md) | Issue、开发、检查和PR提交流程。 |
| [样例](../examples/README.md) | asc-comm API使用样例入口，包含AIV直驱URMA WriteNbi/ReadNbi点对点通信样例。 |
