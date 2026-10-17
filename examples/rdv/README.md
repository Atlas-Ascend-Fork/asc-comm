# RDV 双控制面批量写样例

本目录包含两个功能等价的批量写样例，数据面完全相同（每条WQE单段`WriteNbi`，弱保序，每批每Channel仅
末条WQE请求CQE），仅在Host控制面使用不同的建链接口：

- `rdv_hccl`：基于HCCL通信域接口。`HcclCommMemReg`注册内存，`HcclChannelDesc`描述通道，
  `MakeMultiChannelHandle(HcclComm, tag, ...)`建链，资源随通信域自动管理，SQ深度为库默认值。
- `rdv_hcomm`：基于HCOMM Endpoint接口。`HcommEndpointCreate`/`HcommMemReg`建端点并注册内存，
  `HcommChannelDesc`（`ubAttr.sqDepth=4096`）描述通道，`MakeMultiChannelHandle(EndpointHandle, ...)`建链，
  资源由样例显式销毁（`DestroyMultiChannelHandle`/`HcommMemUnreg`/`HcommEndpointDestroy`）。

## 样例配置

- 每条WQE单段搬运`DATA_SIZE`（1 MiB）数据，无多SGE；
- 每轮每Peer提交32条WQE，执行`PERF_ITERATION_COUNT`（1）轮，无预热；
- 多AIV核：编译参数`RDV_AIV_CORE_NUM`（1至56），每核独立multi-channel句柄（独立Jetty）；
- 全互连：每个Rank向其余所有Rank写入数据；

## 环境要求

- Ascend 950PR或Ascend 950DT，至少两张NPU；
- 已安装包含共享Jetty控制面及Batch接口的asc-comm开发包；
- 已执行CANN环境脚本，`ASCEND_HOME_PATH`指向该开发包所在环境。

## 编译运行

```bash
# HCCL接口版：默认1核8卡
bash examples/rdv/rdv_hccl/run.sh 1

# HCOMM Endpoint接口版：指定4核
bash examples/rdv/rdv_hcomm/run.sh 4
```

run.sh参数：`<core_num> [rank_num] [ip:port] [first_rank] [local_ranks] [first_device]`，例如4卡：

```bash
bash examples/rdv/rdv_hccl/run.sh 1 4 127.0.0.1:8899 0 4 0
```

也可手动编译：

```bash
cmake -S examples/rdv/rdv_hccl -B build/rdv/rdv_hccl -DRDV_AIV_CORE_NUM=4
cmake --build build/rdv/rdv_hccl -j
./build/rdv/rdv_hccl/rdv_hccl 8
```

## SQ深度指定

- `rdv_hcomm`：环境变量`RDV_SQ_DEPTH`指定SQ深度（SQE条数，默认4096，范围16至8192，库自动对齐2的幂）。
  注意kernel侧读到的深度为WQEBB数，按每个SQE占4个WQEBB（256 B）折算，如4096对应16384；
- `rdv_hccl`：`HcclChannelDesc`无深度字段、channel config亦无深度项，`RDV_SQ_DEPTH`不生效，使用库默认深度
  （8192 SQE，即32768 WQEBB），设置该变量时打告警提示。

## 链路层选择

两个样例均支持`RDV_NET_LAYER`环境变量：不设置时从第0层开始扫描rank graph取每个Peer的首条UB_CTP链路；
设为`1`时从第1层开始扫描（跳过第0层）。运行时会打印每个Peer在每一层的链路数（`links=`总数、
`ubctp=`其中UB_CTP链路数）：

```bash
RDV_NET_LAYER=1 bash examples/rdv/rdv_hccl/run.sh 1
```

程序通过`fork`启动本机rank并与Device一一对应，每个rank向所有Peer写入数据，`Drain`完成并经过全局
Barrier后逐字节校验。每个rank输出一行`rdv write verified`，并打印实际的SQ/CQ深度。
