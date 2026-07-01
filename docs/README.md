# asc-comm 文档

## 目录说明

```text
docs/
├── api/                 # API 参考文档
└── guide/               # 使用、构建与测试指南
```

## 文档入口

| 文档 | 内容 |
| --- | --- |
| [API 参考](./api/README.md) | asc-comm 当前公开接口列表。 |
| [Hcomm 使用说明](./guide/hcomm_usage.md) | Hcomm 点对点通信接口的基本使用流程。 |
| [构建与测试](./guide/build_and_test.md) | CANN 环境、构建脚本和 UT 构建说明。 |
| [样例](../examples/README.md) | asc-comm API 使用样例入口。 |

## 说明

asc-comm 当前公开接口集中在 `include/aicore/hcomm/`，文档优先覆盖 Hcomm AICore 侧点对点通信接口。后续新增模块时，应同步补充 API 索引、使用说明、样例和测试说明。
