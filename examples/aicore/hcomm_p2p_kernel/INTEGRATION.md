# 完整算子集成说明

本目录提供asc-comm AICore Hcomm的Kernel侧样例。完整算子用例需要由asc-comm、hcomm和devkit/CANN算子工程共同组成。

## 组件边界

| 组件 | 作用 | 来源 |
| --- | --- | --- |
| Kernel侧Hcomm调用 | 在AICore中调用`Init`、`WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`、`Commit`、`Drain`。 | 本目录`hcomm_p2p_kernel.cpp` |
| 通信通道创建 | 创建通信域、Endpoint、Channel，并生成传给Kernel的`ChannelHandle`。 | hcomm control-plane / comm opdev接口 |
| 自定义算子工程 | 生成算子原型、Host Tiling、Kernel文件、单算子API和编译脚本。 | devkit/CANN `msOpGen` |
| 单算子调用 | 申请输入输出、workspace，调用两段式`aclnn*GetWorkspaceSize`和`aclnn*`接口。 | devkit/CANN单算子API调用流程 |

## 推荐集成流程

1. 使用devkit/CANN `msOpGen`创建自定义算子工程。
2. 在生成的`op_kernel/` Kernel实现中引入本样例的Hcomm调用逻辑。
3. 在Host/Tiling侧为Kernel传入必要参数：`ChannelHandle`、源/目的GM地址、notify地址和值、传输长度等。
4. 通过hcomm control-plane创建通信资源。AIV场景通常通过通信域资源和`HcclChannelAcquire`获取`ChannelHandle`，或按hcomm基础资源接口创建Endpoint/Channel。
5. 编译并安装自定义算子包。
6. 使用单算子API调用程序在所有参与rank上拉起算子。

## 参数传递建议

Kernel侧至少需要以下信息：

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

`ChannelHandle`不能在Kernel内自行创建，必须来自外部通信资源创建流程。GM地址需要对应已经注册或可被通道访问的本端/远端通信内存。

## 当前仓验证范围

本仓可验证AICore Hcomm API行为：

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh -t
```

端到端算子运行依赖外部算子工程和hcomm control-plane，不在当前仓的`build.sh -t`范围内。
