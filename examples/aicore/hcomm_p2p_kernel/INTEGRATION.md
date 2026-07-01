# 完整算子集成说明

本目录提供 asc-comm AICore Hcomm 的 Kernel 侧样例。完整算子用例需要由 asc-comm、hcomm 和 devkit/CANN 算子工程共同组成。

## 组件边界

| 组件 | 作用 | 来源 |
| --- | --- | --- |
| Kernel 侧 Hcomm 调用 | 在 AICore 中调用 `Init`、`WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`、`Commit`、`Drain`。 | 本目录 `hcomm_p2p_kernel.cpp` |
| 通信通道创建 | 创建通信域、Endpoint、Channel，并生成传给 Kernel 的 `ChannelHandle`。 | hcomm control-plane / comm opdev 接口 |
| 自定义算子工程 | 生成算子原型、Host Tiling、Kernel 文件、单算子 API 和编译脚本。 | devkit/CANN `msOpGen` |
| 单算子调用 | 申请输入输出、workspace，调用两段式 `aclnn*GetWorkspaceSize` 和 `aclnn*` 接口。 | devkit/CANN 单算子 API 调用流程 |

## 推荐集成流程

1. 使用 devkit/CANN `msOpGen` 创建自定义算子工程。
2. 在生成的 `op_kernel/` Kernel 实现中引入本样例的 Hcomm 调用逻辑。
3. 在 Host/Tiling 侧为 Kernel 传入必要参数：`ChannelHandle`、源/目的 GM 地址、notify 地址和值、传输长度等。
4. 通过 hcomm control-plane 创建通信资源。AIV 场景通常通过通信域资源和 `HcclChannelAcquire` 获取 `ChannelHandle`，或按 hcomm 基础资源接口创建 Endpoint/Channel。
5. 编译并安装自定义算子包。
6. 使用单算子 API 调用程序在所有参与 rank 上拉起算子。

## 参数传递建议

Kernel 侧至少需要以下信息：

```cpp
AscendC::ChannelHandle channel;
GM_ADDR remoteDst;
GM_ADDR localSrc;
GM_ADDR localDst;
GM_ADDR remoteSrc;
GM_ADDR notifyAddr;
uint64_t notifyVal;
uint64_t len;
```

`ChannelHandle` 不能在 Kernel 内自行创建，必须来自外部通信资源创建流程。GM 地址需要对应已经注册或可被通道访问的本端/远端通信内存。

## 外部资料映射

- devkit 算子工程：参考 `asc-devkit/docs/guide/编程指南/高级编程/Aclnn算子工程化开发/创建算子工程.md`。
- devkit 单算子调用：参考 `asc-devkit/docs/guide/编程指南/高级编程/Aclnn算子工程化开发/单算子API调用.md`。
- devkit 通算融合样例说明：参考 `asc-devkit/docs/guide/算子实践参考/SIMD算子实现/融合算子编程/通算融合/算子实现.md`。
- hcomm AIV 通信资源：参考 `hcomm/docs/zh/comm_op_dev_guide/aiv_comm_op_dev/create_res.md`。

## 当前仓验证范围

本仓可验证 AICore Hcomm API 行为：

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh -t
```

端到端算子运行依赖外部算子工程和 hcomm control-plane，不在当前仓的 `build.sh -t` 范围内。
