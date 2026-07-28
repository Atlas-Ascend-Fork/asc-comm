# CCU 直驱 AllGather Mesh1D Mem2Mem

本样例演示如何用 asc-comm 的 CCU 直驱 launch 模型跑一个 Mesh1D Mem2Mem 的
AllGather，只展示 **数据面**：

```text
Host 建域 / 取 CCL buffer / 建 UBC_CTP channel / 取 token
  -> Host 注册 CCU kernel（HcommCcuKernelRegister*）+ 构造 taskArgs
  -> HcommCcuKernelLaunch 下发 CCU kernel
  -> Mesh1D Mem2Mem AllGather（地址交换 -> Write + 本地 GroupCopy -> 后同步）
  -> 多卡逐元素校验
```

数据面 kernel 对齐 `asc-devkit/pr_4261` 的 KFC 参照实现
`ccu_kernel_kfc_all_gather_mesh1d_mem2mem`，改写为 asc-comm 的 `ccu::` 自由函数
表示层 API。传输结构：**event 数组按 rank 分组**（每 16 个 rank 一个 event、每个
rank 记自己的 bit）+ **write/wait 分离**（先 `DoAllGatherWrite` 发全部远端
`ccu::Write` 并对自身 `EventRecord`，再无条件本地 `GroupCopy`，最后
`DoAllGatherWait` 统一 `EventWait`）。`CCU_IF(sliceSize != 0)` 位于最外层，
per-rank 循环内无 `CCU_IF`。无 repeat 循环、无 `isInputOutputEqual` 分支。

> 本样例不使用 `Hccl<CCU>::InitV2` / `AllGather<true>` / KFC 常驻 server。
> 装机 CANN 的 `InitV2` 不识别新版 KFC `OpResCtx`，无法激活常驻 server；因此
> 走 host 直驱 + 数据面 kernel 的路径。

## 与 examples/custom_ccu_allgather 的区别

两者都是「host 直驱 CCU AllGather、只做数据面、注册 + `HcommCcuKernelLaunch`」。
custom 是单发 + 单 event；本样例对齐 pr_4261 KFC 参照，用 event 数组 + write/wait
分离（>16 rank 也正确）。主要区别：

| 维度 | custom_ccu_allgather | 本样例（ccu_direct_allgather_mem2mem） |
|---|---|---|
| host 组织 | 拆成 `allgather.cc` + `utils.cc` + `exec_op.cc` 三文件 | 单文件 `testcase/main.asc`，函数分层（AllocAlgResource / InitAlgResourceCtx / GetCcuKernel / AllGatherCustom） |
| 运行入口 | 固定 `bash run.sh`（无命令行参数），`sendCount=devCount` | 命令行 `--devices` / `--count` / `--timeout-ms`，可任意 count 与 rank 数 |
| 测试数据 | `input[i] = deviceId`（同一 rank 内所有元素相同） | `input[i] = rank*1000000 + i + 1`（逐元素不同） |
| 校验 | 打印 input/output，人工比对 | 逐元素自动校验 + 打印（`setprecision(9)`），不符即报 mismatch |
| 超时保护 | 无 | `alarm(timeoutMs)`，超时以 124 退出，避免挂死 |
| 数据来源 | AllGather 原始模板 | 移植 pr_4261 的 mem2mem 模板（taskArgs 保留 repeat/尾块等预留字段） |

关键说明：custom 的测试数据「同一 rank 内全相同」，无法暴露 remote write 只写对
部分元素的问题；本样例改用逐元素不同的数据，能真正校验每个元素，测试更严格。

> 移植过程中曾把 remote write 循环放进逐 peer 的 `CCU_IF`，并加入
> `isInputOutputEqual` 分支，导致 CCU 条件图/事件依赖错误、单方向 remote write
> 整段丢数据。改回「单个 `CCU_IF` 包住整段传输」后修复。详见
> [docs/CCU_IF_bug_analysis.md](./docs/CCU_IF_bug_analysis.md)。

## 支持范围

- Ascend 950PR/DT，AICore 编译架构固定为 `dav-3510`
- CANN 9.1 或更高版本，需提供 CCU HCCL client、资源和 channel 接口
- FP32、单 Die、单 mission、Mesh1D、1 到 16 个 rank
- 单次传输、in≠out。尾块（count 非 memSlice 整数倍）由 host 侧 `CalGoSize`
  的 m/n/p 切分覆盖，已验证正确
- 不含 pr_4261 的 repeat 分片、`isInputOutputEqual` 原地优化：这些字段仍在
  taskArgs / LoadArgs 中预留（加载但不使用），本样例场景用不到。若日后要加回，
  须避免把 `CCU_IF` 放进 rank 循环（见上文 bug 分析）
- 分组搬运 config 统一为 `loopCount=8`、`memSlice=4096*8`，host 侧 `CalGoSize`
  与 kernel 侧 `GroupCopy` 必须一致

## 构建和运行

```bash
source /usr/local/Ascend/cann/set_env.sh
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
cd examples/ccu_direct_allgather_mem2mem
env DEVICES=0,1 COUNT=256 ./run.sh
```

`HCCL_OP_EXPANSION_MODE=CCU_SCHED` 让 HCCL 在建域时走 CCU 调度路径并分配 CCU
所需的 CCL buffer。`run.sh` 已在内部导出该变量（可被外部覆盖）；直接运行样例
二进制时务必先设置它，否则 `HcclGetHcclBuffer` 返回空 buffer、样例在资源初始化
阶段失败。

`run.sh` 还会把 CANN 进程日志（plog）重定向到样例目录下的 `./log/`
（`ASCEND_PROCESS_LOG_PATH`，可被外部覆盖），便于排查。

### 编译与运行分开

```bash
# 只编译
env BUILD_ONLY=1 ./run.sh

# 只运行（直接跑二进制时必须自己 export 变量）
export HCCL_OP_EXPANSION_MODE=CCU_SCHED
./build/ccu_direct_allgather_mem2mem --devices 0,1 --count 256 --timeout-ms 30000
```

参数：`--devices` 逗号分隔设备号（元素个数即 rank 数），`--count` 每 rank 的
FP32 元素数，`--timeout-ms` 超时毫秒（超时以状态码 124 退出）。

## 目录结构

```text
inc/
  common.h          OpParam / CcuKernelArgBase / 资源序列化 / CcuKernelInfo
  binary_stream.h   资源上下文序列化
  log.h             日志与返回码检查宏
op_host/
  data_utils.*      输入生成与输出校验
op_kernel_ccu/
  ccu_kernel.*      Mesh1D Mem2Mem AllGather 数据面 kernel（ccu:: 表示层）
testcase/
  main.asc          Host 直驱 runner（建域/资源/注册/launch/校验）
docs/
  CCU_IF_bug_analysis.md   CCU_IF 结构导致单向丢数据的问题分析
```

每个 rank 的输入为 `rank * 1000000 + element_index + 1`，输出按 rank 顺序逐元素
校验。`TIMEOUT_MS` 默认 30000。

## 验证记录（KFC 对齐版，Ascend 950DT_9574，dav-3510，CANN 9.1，2026-07-28）

`HCCL_OP_EXPANSION_MODE=CCU_SCHED`，均逐元素校验通过、退出码 0：

| 场景 | 命令 | 结果 |
|---|---|---|
| 双卡 小数据 | `DEVICES=0,1 COUNT=256 ./run.sh` | rank0/1 verified |
| 双卡 跨多 memSlice | `DEVICES=0,1 COUNT=8192 ./run.sh` | rank0/1 verified |
| 双卡 尾块（非整块） | `DEVICES=0,1 COUNT=65539 ./run.sh` | rank0/1 verified |
| 单 rank（本地拷贝） | `DEVICES=0 COUNT=256 ./run.sh` | rank0 verified，2s |

复跑其它规模：

```bash
env COUNT=1 DEVICES=0,1 ./run.sh
env COUNT=65536 DEVICES=0,1 ./run.sh
env COUNT=0 DEVICES=0,1 ./run.sh
```

真机测试时请保存：CANN 版本与安装路径、`aclrtGetSocName` 返回值、设备列表和
rank 数、完整构建命令、运行日志及逐 rank 校验结果。
