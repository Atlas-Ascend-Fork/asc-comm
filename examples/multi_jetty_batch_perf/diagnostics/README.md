# Historical diagnostics / 历史诊断

当前验证使用上级目录。此目录只用于历史诊断与SIMD复现；旧命令路径需要加 diagnostics/，不支持 matrix profile。

# Hcomm SIMD / SIMT 多 Jetty Batch 性能用例

[English](./README_en.md)

本用例比较同一 AI Core 上 SIMD 顺序驱动多个 Jetty 与 SIMT 分组驱动多个 Jetty 的提交开销和完成带宽，
覆盖 `WriteNbi`、`ReadNbi`、`WriteWithNotifyNbi`。每个 rank 创建一个 HcclComm，批量获取同一 peer 的
多个 Channel；每个活跃 BatchHandle 独占一个 Channel，使用独立的收发数据区域。
用例支持 2 卡或 8 卡，不需要 rank table；8 卡按 0→1、2→3、4→5、6→7 配对，分别输出结果。

## 当前完整验证矩阵（CompletionV1，72项）

使用新配置`simt-matrix-bs1/8/32/64/128/256`，不要用旧`simt-full`替代本轮验收。
Write/Read/Notify × J1/J2/J4/J8 × BS1/8/32/64/128/256，共72项；固定64B、每Jetty1024项计时请求、warmup请求96向batch对齐、2卡。
每个构建仅实例化一个parallel VF，最大lane容量依次为32/64/256/512/1024/1024；不编SIMD或SIMT-Serial。
六个构建各受5分钟及1152B/32寄存器原门槛约束，不隐式增加模板矩阵或放宽门槛。

所有配置显式使用仓库Hcomm及CompletionV1，沿用三个跨Jetty边界和已删除二次SQ扫描的发布路径。
每项从新建Channel开始。同一case的CompletionSet在stream上的launch间保留：首launch登记全部Jetty身份，后续仅登记自身窗口并接续共享CQ游标。
J8/BS256为两次各4Jetty的kernel，输出KernelLaunches=2/MaxConcurrentJettys=4；其他组合单kernel。
CQ快照计数按所属launch结束时刻检查，不要求第一组快照提前包含第二组尚未发布的完成数。
Write/Read的BB目标为requests+requests/batch；Notify为2×requests。完整检查所有预热及计时数据，Read检查发起端，Notify还检查每条通知值。

产物身份为AuditVersion=10、CompletionVersion=1、MatrixBatch=<N>、Profiling=0。
关闭逐CQE分项计时与CQ原始采样，保留原issue/completion时钟和计数/数据校验；Measurement=RawDeviceTicks，单位为ticks及bytes/tick，不能直接将历史SIMD的ns/GB/s标签作等价换算。
每个尺寸输出12条RESULT和一条MATRIX_SUMMARY，功能失败继续收集该尺寸的剩余组合，最终退出非零；超时/设备异常时停止后续任务，其余记NOT RUN。
总共应得到72个唯一配置，不能用部分PASS代替全量PASS。SIMD复用已有54项基线，J1无旧SIMD对照。

```bash
# 示例：只编BS32，随后跑该尺寸的12项。六组完整qrun命令见当前执行卡。
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --matrix-batch 32
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-core-parallel bash run.sh simt-parallel --build-profile simt-matrix-bs32 --api all --batch-sizes 32 --jetty-counts 1,2,4,8 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

[当前完整执行卡](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/current/run.md)。本地仅主机测试，目标CANN构建、NPU正确性、资源与性能均需回传验证。


## 构建

需要支持 dav-3510 的 Ascend 950、CANN/ASC 编译器和匹配的 Hcomm 头文件/运行库。
本轮串行范围实验使用下文的独立 scope 配置。普通 SIMT 排错可选择快速矩阵：

~~~bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/multi_jetty_batch_perf
bash build.sh --backend simt --fast
~~~

| 构建命令 | 配置 / 输出目录 | 实际编译范围 |
|---|---|---|
| bash build.sh --backend simt --scope control | simt-scope-control / build/simt-scope-control/ | A/N 对照，只编译一个 BS256/容量1024 VF；使用 full UB 布局 |
| bash build.sh --backend simt --scope publisher | simt-scope-publisher / build/simt-scope-publisher/ | P 候选，独立编译一个 BS256/容量1024 VF；不修改对照产物 |
| bash build.sh --backend simt --fast | simt-fast / build/simt-fast/ | SIMT parallel 的 BS32/256 两个 VF；不编 SIMD |
| bash build.sh --backend simt | simt-full / build/simt-full/ | 完整 SIMT；不编 SIMD |
| bash build.sh --backend simd | simd-full / build/simd-full/ | 只编 SIMD；不编 SIMT |
| bash build.sh --fast | all-fast / build/all-fast/ | SIMD + 两个 SIMT parallel VF |
| bash build.sh | all-full / build/all-full/ | SIMD + 完整 SIMT |

运行时用 --build-profile 选择同名配置。SIMT-only 配置下 run.sh all 只运行 SIMT；显式选择未编译的模式会在设备初始化前报错。
fast 配置在未指定 --batch-sizes 时默认使用 32,256，不包含 SIMT-Serial。

每种配置独立保存，--clean 只删除该配置，普通构建复用已有产物。旧 build/multi_jetty_batch_perf 保留。
不指定 --build-profile 时，run.sh 优先使用 all-full，否则兼容旧 binary；不会猜测选择某个 SIMT-only 配置。
构建输出配置及耗时；运行输出 binary 配置、路径和哈希。binary 的 --build-info 无需初始化设备。

BUILD_JOBS 控制构建任务并行度。旧 R1/R2 运行时扩展已撤销；scope 候选使用编译开关隔离。
这版消除未选后端的编译，不宣称已解决 SIMT VF 内部编译膨胀；实际耗时变化待目标构建返回。
[CMakeLists.txt](CMakeLists.txt) 默认使用 ASCEND_HOME_PATH 下的 SDK 头文件和库；publish-audit例外，自动优先使用当前仓库的私有Hcomm头文件布局，运行库仍来自SDK。

[固定 SIMD 基线](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/reference/simd_performance_baseline.md)
已保存 2026-09-07 完整已记录多 Jetty 矩阵（54 项 PASS）。SIMT 迭代复用相同配置的历史值，不默认重复编译/运行 SIMD。
该基线使用 1024 次计时请求，不能直接与 512 次排错结果比较收益。

## 运行

### 可选 ACL 配置（历史排错用途）

`MULTI_JETTY_ACL_CONFIG` 可给普通配置的 `aclInit` 提供 JSON 路径；未设置或为空时使用默认初始化。
`acl_simt_stack_2048.json` 保留用于追溯此前354/334排错，不是当前实验的运行前提。
scope 配置拒绝额外 ACL 配置及 trace，禁止通过扩大栈来挽救实验产物。
旧版1176B资源报告不代表恢复后的当前VF；每个scope构建必须取得自己的资源报告。

当前串行控制下的小矩阵，可配合 fast build：

```bash
unset MULTI_JETTY_BATCH_TRACE
MULTI_JETTY_PARALLEL_CONTROL=serial-commit \
    bash run.sh simt-parallel --build-profile simt-fast --api write --batch-sizes 32,256 \
    --jetty-counts 4,8 --iterations 512 --warmup 96 --payload-bytes 64
```

该命令是诊断/回归入口，不表示当前源码已经通过目标验收；以实际 `RESULT Status` 为准。
仅在需要建立新 SIMD 基线时，显式构建两个后端后进行同配置比较：

```bash
bash build.sh
MULTI_JETTY_PARALLEL_CONTROL=serial-commit \
    bash run.sh all --build-profile all-full --api all --batch-sizes 32,256 --jetty-counts 4,8 --iterations 512
```

完整构建下，执行默认矩阵或单独选择模式：

```bash
bash build.sh
unset MULTI_JETTY_PARALLEL_CONTROL MULTI_JETTY_BATCH_TRACE
bash run.sh all --build-profile all-full
bash run.sh simd-serial --build-profile all-full --api notify --batch-sizes 32 --jetty-counts 4
bash run.sh simt-serial --build-profile all-full --api read --batch-sizes 32 --jetty-counts 4
bash run.sh simt-parallel --build-profile all-full --api write --batch-sizes 32 --jetty-counts 4 --devices 8
```

### 模式与并行范围

| 模式 | 执行方式 |
|---|---|
| `simd-serial` | SIMD 顺序构造请求并提交各 Jetty 的 batch |
| `simt-serial` | 一个 SIMT group 依次处理各 Jetty，组内并行构造一批请求 |
| `simt-parallel` | 每个 Jetty 对应一个 SIMT group；默认各组并发推进 |
| `all` | 运行所选配置已编译的后端；SIMT-only 自动跳过 SIMD。完整 SIMT 配置可加 `--include-simt-serial` |

SIMT 每个 Jetty 的 lane 数目前等于 BatchSize，每 lane 每批恰好构造一个请求。
`--iterations` 是每个 Jetty 的计时请求数，不是 batch 数；每个 Jetty 的计时 batch 数为
`iterations / BatchSize`。当前没有独立的 `--lanes-per-jetty` 参数。
不能通过连续调用现有 Append 来让一个 lane 在同一批内构造多个不同请求，现有预留槽位按 lane rank 定位。

每次 SIMT-parallel kernel 最多使用 1024 lanes，单次 Jetty 数上限为 `1024 / BatchSize`。
BS256/J8 因此分成同一 stream 上的两次 kernel，每次四个 Jetty；输出带有 `2KernelLaunches`，
不能将该行理解为八个 Jetty 同时运行。用例没有把每个 Jetty 分配到不同 AI Core。

### 参数

| 参数 | 默认值 | 含义与范围 |
|---|---|---|
| `--api` | `all` | `write`、`read`、`notify` 或 `all` |
| `--build-profile` | 优先 all-full，否则旧 binary | 显式选择 all-full/all-fast/simt-full/simt-fast/simd-full；不回退到其他配置 |
| `--batch-sizes` | 完整配置为 `1,8,32,64,128,256`；fast 为 `32,256` | 从可用集合选择；每个值须整除 iterations |
| `--jetty-counts` | `1,2,4,8` | 每个 peer 的 Channel 数，从该集合选择 |
| `--iterations` | `1024` | 每 Jetty 的计时请求数，正整数 |
| `--warmup` | `96` | 每 Jetty 的预热请求数，非负；脚本对每个 BatchSize 向上取整到整批 |
| `--payload-bytes` | `64` | 每个请求的数据字节数，正整数 |
| `--devices` | `2` | `2` 或 `8` |
| `--log` | `results/<mode>_<timestamp>.log` | 保存 stdout 和 stderr |
| `--include-simt-serial` | 关闭 | 在 `all` 中加入 SIMT-Serial |

例如 requested warmup=96 时，BS32 实际预热 96 项，BS256 实际预热 256 项。

## 独立串行范围实验

目标：确认普通 lane 的 Append 是否可以移出跨 Jetty 串行区。旧 `serial-publisher-commit` / `commit-only`
运行时模式已退役，使用这些值会报错。P 的代码不会编译进 A/N 的 VF。

| 调度 | 构建配置 | MULTI_JETTY_PARALLEL_CONTROL | 行为 |
|---|---|---|---|
| A | simt-scope-control | serial-commit | 恢复624abdd的完整 Append+Commit 顺序控制 |
| N | 与A相同的binary | 空值或未设置 | 原来的各Jetty并行Append/Commit，验证原故障是否复现 |
| P | simt-scope-publisher | publisher-commit | 普通lane并行Append，每个Jetty的publisher Append+完整Group Commit依次执行 |

两种配置均限制为 Write/64B/BS256/J4、1024计时请求、2卡、warmup96（对齐到256），trace关闭、默认ACL配置。
full、fast、scope目录互不覆盖；scope不启用fast的workspace guard布局。A/N的VF与辅助函数源码恢复到624abdd，
只裁剪外层dispatch；不承诺新工具链下生成代码与历史binary逐字节相同。

在用例目录构建，任一命令失败或超时即停止，勿继续构建下一项或运行：

```bash
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope control
sha256sum build/simt-scope-control/multi_jetty_batch_perf > /tmp/multi-jetty-scope-control.sha256
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope publisher
sha256sum -c /tmp/multi-jetty-scope-control.sha256
```

构建脚本检查只有一个 `<256,1024>` VF，栈报告不超过1152B、使用寄存器不超过32。
1152B是本实验的保守门槛，不表示已查询目标实际分配。缺报告或资源超限时输出 `READY=NO` 并失败，
不生成当前binary的运行凭据。增量构建只有在设备object哈希不变时才能复用旧报告。
`scope-build.log` 保存构建输出，`scope-resource.txt` 保存检查结果；`run.sh` 校验资源通过时的binary哈希。
READY=YES只表示可以进入目标运行，不代表正确性PASS。BUILD_JOBS默认scope为2。

下面是三种单次运行形式，正式次序与停止条件以
[当前执行卡](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/current/run.md)为准：

```bash
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=serial-commit bash run.sh simt-parallel --build-profile simt-scope-control --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL= bash run.sh simt-parallel --build-profile simt-scope-control --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-commit bash run.sh simt-parallel --build-profile simt-scope-publisher --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

P在首次预热Append前保留一次不计时的全block同步，确保所有Handle初始化完成。每批保留每个turn前的block
同步与最后的批次边界；没有失败组提前退出这些同步。完整Commit仍由选中Jetty的全部lane参与。
预热和计时阶段都使用P路径，Handle/Drain、库内fence/DCCI、WQE/head算法保持原样。
P不插入SQ/Batch trace；入口强制关闭trace。错误传播依赖当前Append错误由共享reservationStatus决定的组内一致性。

**头文件前提：** 实际group BatchPostWqe必须含210c066精简，为非collective Append；不兼容旧版内部有group.sync的Append。
本轮不修改或重装库，沿用已确认替换的头文件环境。

最多五组A/N/P。A失败或出现新执行异常立即停止；P发生原故障则候选不充分；N没有复现则实验不能证明P有效。
A/P均通过且N复现时，才比较A/P性能；五次通过不能证明长期稳定。约10%的IssueTime下降是继续细分的初步目标。
不重跑SIMD，也不改变batch/lane映射来换取PASS。确认P之后才决定是否进一步拆分Commit，不自动增加试验组合。

## Q：普通SQ维护并行准备

`--scope sq-prepare` 生成独立的 `simt-scope-sq-prepare` 产物，要求
`MULTI_JETTY_PARALLEL_CONTROL=publisher-sq-prepare`，输出 `Schedule=Q`。
固定参数、workspace布局、线程数、Handle/Drain与资源门槛均与P相同；不重编/覆盖已有P，不替换SDK头文件。

Q在各Jetty普通lane Append后并行执行普通lane自己的SQ维护，再由各自publisher扫描普通WQE对应的255个BB。
publisher自身的Append、生产者SQ维护、最后2个BB的扫描、head维护、DWQE发布和下一批预留仍连续串行。
两段扫描合计仍为257个BB，未删除维护调用。准备阶段新增两次group同步和publisher的完成fence，
原Commit同步及每批五次block同步保留。预热/计时均覆盖，错误组仍参与所有block同步。

拆分仅在用例simt_kernel.cpp的Q编译分支，发布部分取自35335c7的Group Commit，复用现有内部函数；
不新增公共API。P路径不含Q代码。两段扫描均按sqDepth取模，不改head或WQE算法。

```bash
# 用例目录内只编译Q，退出0且出现READY=YES才运行；失败/超时即停，不调栈。
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope sq-prepare
# Q单次命令；排队环境使用当前执行卡中的qrun形式。
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-sq-prepare bash run.sh simt-parallel --build-profile simt-scope-sq-prepare --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

上限7次：旧P一次、Q五次、旧P一次，180秒/次、累计15分钟（不计排队）；不重跑A/N/SIMD。
任意P失败则对照不成立，任意Q失败则停止候选，新执行异常同样停止。全部通过后比较Q中位数与两次P及历史P范围；
约10%的IssueTime下降是初步收益目标，五次通过不是长期稳定证明。Q原数据错误只说明本候选不充分，不能直接归因某条DCCI。
正式次序与回传以[当前执行卡](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/current/run.md)为准，不自动追加实验。

## R：取消publisher轮流执行

当前实验以已通过的Q为对照，只取消跨Jetty的publisher turn。`--scope parallel-publish` 生成独立的
`simt-scope-parallel-publish` 产物，要求 `MULTI_JETTY_PARALLEL_CONTROL=publisher-parallel`，输出 `Schedule=R`。
普通SQ准备完成后，各Jetty同时进入各自的publisher Append和发布过程；每批block同步由五次减为首尾两次。
Q的PrepareOrdinarySq和CommitPreparedOrdinarySq原样复用，Group显式作为辅助函数参数；不改公共Hcomm接口。
fence/DCCI、组内同步、workspace、WQE/head、Handle/Drain及预热/计时参数均保留。

```bash
# 在用例目录只编译R，保留已有Q；失败、超时或READY=NO即停。
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope parallel-publish
# R单次形式；实际Q1/R1–R5/Q2次序及qrun命令见当前执行卡。
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-parallel bash run.sh simt-parallel --build-profile simt-scope-parallel-publish --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

固定Write64B、BS256、每Jetty256 lanes、J4；本轮不加入batch/lane解耦，避免改变验证条件。
上限七次：旧Q一次、R五次、旧Q一次，每rank180秒、累计实际执行15分钟（不计排队）。任意失败或执行异常即停。
全部通过才比较R中位数与两端Q及历史128ns，约10%下降为初步收益目标；不重编Q或重跑SIMD。
R通过仅支持当前配置下取消publisher互斥，仍存在准备阶段和批次边界的跨Jetty同步；失败不能定位具体发布指令。
正式操作仅以[当前执行卡](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/current/run.md)为准，不自动追加实验。

## S：仅publisher核心发布段保留顺序控制

当前候选以已通过的Q为对照。`--scope publish-core`生成独立的`simt-scope-publish-core`，
要求`MULTI_JETTY_PARALLEL_CONTROL=publisher-core`，输出`Schedule=S`。
普通Append/PrepareOrdinarySq不变；turn内仅publisher执行自身Append、SQ维护、head更新和完整DWQE发布。
删除这一段的三次group.sync，保留所有原DCCI/fence。下一批预留和原两次组内收尾同步移到turn外，
各Jetty独立完成；每批原五次block同步保留，最后一次保证所有收尾结束才进入下一批。

每批在turn前固定组内一致的reservationStatus，避免publisher更新下一批预留状态使本批集体调用分歧。
当前非collective Append只可能因共享reservationStatus失败；S依赖已确认的210c066头文件环境。
预热与计时均使用S；不改公共Hcomm接口、Group成员、SDK、workspace、WQE/head算法或batch/lane映射。

```bash
# 用例目录只编S，保留已有Q。失败、超时或READY=NO即停。
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope publish-core
# 单次S；实际Q1/S1/Q2顺序及qrun命令以当前执行卡为准。
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_PARALLEL_CONTROL=publisher-core bash run.sh simt-parallel --build-profile simt-scope-publish-core --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

只编一个256/1024 VF，沿用stack<=1152B、registers<=32门槛。先Q1→S1→Q2，任意失败即停。
只有三项PASS且S1耗时不高于两端较快Q的90%，才补S2/S3；最多五项、累计实际执行10分钟（不计排队），
每rank180秒。收益不足则停止，不重编Q或重跑A/N/P/R/SIMD。
S检验缩小后的整体保护范围是否充分；通过不证明每一处等待可单独删除，也不代表完全独立Jetty推进。
本节为接口说明，完整步骤只维护在[当前执行卡](https://gitcode.com/pengqihw/DesignDocs/blob/docs/asc-comm-simt-notify-atomic-run-guide/docs/shmem/implementation/simt_dwqe/current/run.md)。

## 共享CQ完成管理 CompletionV1（publish-audit）

发布调度沿用完整通过的e504f4a：四个Jetty同轮并发，每批保留普通SQ准备完成、所有publisher发布函数返回、Plan/组内清理完成3个block屏障。
本轮基于13d9c91完整PASS版本实施L3维护删减：comm Group BatchCommit及用例PrepareOrdinarySq/CommitPreparedOrdinarySq/PublishPreparedPublisherSq删除publisher二次SQ扫描。各lane自身SQ维护、所有fence/group/block同步、head/DWQE发布及完成处理保持。Write/BS256每Jetty每批少257次DCCI；这不是删除三个跨Jetty屏障。历史Q/R/S辅助函数也共享此改动，新编版本不能当旧二进制对照。保留原完成诊断探针。仅本用例在包含私有Hcomm头时启用统计钩子；记录位于原4096B工作区最后256B，和comm有效状态不重叠。
沿用simt-scope-publish-audit、一个256/1024 VF，标识为AuditVersion=9、CompletionVersion=1、Schedule=S-parallel-3barriers。旧产物会被run.sh拒绝。
COMPLETION_PROFILE分别输出Warmup/Timed的Calls、Probes、NotReady、Processed、ProbeTicks、NotReadyProbeTicks、AccountTicks、AckTicks、ProgressTicks及OtherProgressTicks。NotReadyProbeTicks是ProbeTicks的子集；AccountTicks只累计成功处理路径，失败时不用于性能归因。
COMPLETION_PROFILE_TAIL给出原issueEnd到completionEnd区间，以及扣除ProgressTicks的余量；余量包含诊断累计写入、Wait/Drain调度和同步等，不能直接当硬件时间。ProgressTicks不含末尾诊断记录写入。逐条时钟探针有开销，此版仅用于耗时分布诊断，不计作优化性能。
COMPLETION_PROFILE_CHECK检查计数和时间关系；预热Processed应为1024、Timed为4096。记录在原计时结束后写回，host按累计值减预热得到计时阶段。
CMake为此profile复制当前仓库Hcomm头文件到build目录内的comm-headers，以绝对路径宏指定SIMT入口，内部实现沿相对路径引用；不覆盖SDK或重编CANN。
入口立即检查Completion版本；若其他头提前包含Hcomm则明确报错，避免静默使用SDK旧实现。
每次通过build.sh重新配置/构建，可同步更新私有头文件。其他profile保持原SDK搜索路径。

完成接口均由同一个指定lane操作，CompletionSet通过参数显式传递：

| 接口 | 行为 |
|---|---|
| InitCompletionSet | 以Channel列表及128B对齐、清零的GM工作区初始化一次；每实际CQ一份消费进度，按完整20位JFS ID映射 |
| PrepareCompletion(set, jettyIndex, wqeCount, bbCount) | 发布前为一个Jetty登记窗口、检查独立SQ容量及共享CQ总额度；只要求这个Jetty上个窗口已完成 |
| Progress(set, budget, processed) | 最多消费budget条已就绪CQE，不等待未就绪CQE；按归属记账，多个CQ轮转检查 |
| WaitCompletion(set, jettyIndex) | 等待所选Jetty的已登记目标，同时推进共享CQ；其他Jetty不用全部完成 |
| Drain(set) | 可选的整组等待；本用例在预热和计时结束使用，不是库强制的发布顺序 |

每Jetty窗口全部成功后，将它的SQ tail回收到登记的BB终点，不从混合entryIdx推算回收位置。
慢Jetty不阻止快Jetty完成、回收并登记下个窗口；但接口没有后台线程，调用者必须安排进度所有者执行。
同一集合的Init/Prepare/Progress/Wait不能并发调用，同CQ不能混用旧Drain、单次接口ReservePost自动轮询或SIMD消费者。
第一版只接入默认CQE开启的Write Batch，一个Jetty同时一个登记窗口，必须按登记数量发布；稀疏CQE、未发布尾项和
无完成NOP尾部不支持。工作区跨预热/计时保留，须从新建未使用Channel开始；错误后结束该实例，不清零状态继续跑。
CompletionSet不改发布group的UB预留状态。长期复用时，若旧batch在回收前已把下一次预留标为失败，
需在本Jetty窗口完成后按原group合同重新MakeBatchHandle再提交；当前5批短用例不触及此边界。
当前batchSize仍等于每Jetty lane数；本实验用publisher-core-parallel选择固定对照，正常接口用户不需要填写CQ地址/ID。

```bash
# 用例目录只编当前profile；失败、超过5分钟或READY=NO即停。
timeout --signal=TERM --kill-after=10s 5m bash build.sh --backend simt --scope publish-audit
# 只运行一次SQ重复维护删减验证；回传COMPLETION_PROFILE等输出，不自动重复。完整qrun命令在当前执行卡。
env MULTI_JETTY_ACL_CONFIG= MULTI_JETTY_BATCH_TRACE=0 MULTI_JETTY_BATCH_PERF_TIMEOUT_SECONDS=180 MULTI_JETTY_PARALLEL_CONTROL=publisher-core-parallel bash run.sh simt-parallel --build-profile simt-scope-publish-audit --api write --batch-sizes 256 --jetty-counts 4 --iterations 1024 --warmup 96 --payload-bytes 64 --devices 2
```

资源门槛仍为stack<=1152B/registers<=32。只运行一例，PASS/FAIL都停止并回传，不重编旧Q/S/SIMD。
COMPLETION_STATE输出每Jetty真实累计计数：预热Completed=256、共享Cq=1024、Sq=257；最终Completed/Target=1280、
共享CqConsumed/CqAcknowledged=5120、SqTail/TargetBb=1285。COMPLETION_CHECK自动验证这些条件，任何一项不符都FAIL；
RequestIdentityV1还会检查全部5120项数据。COMPLETION_ERROR保留真正消费位置的CQ/JFS/Jetty、原始三字和status/substatus，
可覆盖原1280条事后取样窗口之外的错误。Kind为0表示提交/准备失败，1资源、2归属、3硬件CQE、4计数/终点、5超时；
无法确认的Jetty或硬件状态显示NA。超时只代表未观察到所需完成，不能断言硬件没有执行。

原PUBLISH_AUDIT/CQ_AUDIT/CQ_SOURCE保留为有限原始取样；CQ_SOURCE不是所有完成数，Sequence不是请求号。
PUBLISH_AUDIT的CqTail现在是CompletionSet统一游标，旧Channel私有CQ tail不再被此路径使用或伪装为单Jetty完成计数。
所有硬件错误都只确认此前已安全记账的CQ前缀，然后停止；未知ID不跳过、不归给其他Jetty。
Measurement=DiagnosticOnly。此版未在本地执行CANN/NPU验证，不声称已解释全部历史status=6或性能已优于SIMD。
本地测试：tests/ut/aicore/hcomm/test_simt_completion_host.py，编译实际完成算法并测试快慢Jetty独立窗口、环回、
错误归属/确认、容量和真实run.sh输出（模拟进程）；它不替代目标cache/DMA验证。

## 其他诊断控制

普通配置支持未设置（并行）、`serial-commit`（完整Append+Commit串行）和`one-active`；scope配置只接受表中调度。

`one-active` 用于同一次 kernel 中的活跃组对照，建议限定 BS32/J4 或 J8；当前跨多次 kernel 的
活跃请求统计/校验不能作为该控制的有效验收口径。示例：

```bash
MULTI_JETTY_PARALLEL_CONTROL=one-active \
    bash run.sh simt-parallel --build-profile simt-fast --api write --batch-sizes 32 --jetty-counts 8 --iterations 512
```

`MULTI_JETTY_BATCH_TRACE` 未设置或为 `0` 时关闭设备 trace；`1` 启用 Batch/CQ 记录，
`full` 增加逐请求 SQ 记录，`expert` 使用完整记录但筛选更紧凑的诊断输出。
这些模式会增加设备工作，并使用诊断 CQ 轮询路径，不用于正式性能对比。
workspace canary 检查需要 fast build 且启用 trace。

```bash
MULTI_JETTY_PARALLEL_CONTROL=serial-commit MULTI_JETTY_BATCH_TRACE=expert \
    bash run.sh simt-parallel --build-profile simt-fast --api write --batch-sizes 32 --jetty-counts 4 --iterations 512
```

`MULTI_JETTY_BATCH_PERF_TIMEOUT_SECONDS` 设置每个 rank 进程的超时，默认 180 秒。

## 结果与性能口径

每个配置、每个 rank pair 输出一条最终 `RESULT`。初始化和预热不计入时间；预热 Drain 完成后开始计时。

| 字段 | 含义 |
|---|---|
| `RUN_IDENTITY` | 运行脚本所在 checkout 的 HEAD 和工作树状态；不能单独证明已安装头文件或 binary 版本 |
| `LanesPerJetty` | SIMD 为 1；SIMT 为 BatchSize |
| `BatchesPerJetty` | 每 Jetty 的计时 batch 数 |
| `TotalDBs` | kernel 报告的计时 batch 门铃数；成功完整矩阵通常为 Jettys × iterations / BatchSize |
| `IssueTime/ns` | 所有计时请求的平均提交时间，包含提交区间内的同步和启用的 trace 开销 |
| `Bandwidth/GB/s` | 有效 payload 总字节数除以提交开始到最终 Drain 完成的时间，不含 Notify 标志字节 |
| `Status` | 综合发送端完成状态和接收端数据/Notify 校验；Read 校验发起端本地结果 |

跨多次 kernel 时先累加各 kernel 的计时区间再计算指标，不包含 host launch 间隙。
只有 `Status=PASS` 的行可用于性能比较。`FailureKind=RemoteValidation` 表示远端数据校验失败；
`CqeStatus=NA` 表示没有对应的失败 CQE 记录，不表示 CQE status 已验证为零。
比较时保持 API、payload、Jetty 数、BatchSize、控制模式、trace 和构建配置一致，并重复运行观察波动。

## 文件

- [main.cpp](./main.cpp)：通信资源、启动分组、数据校验与结果聚合。
- [simd_kernel.cpp](./simd_kernel.cpp)：SIMD 顺序 batch 提交。
- [simt_kernel.cpp](./simt_kernel.cpp)：SIMT serial/parallel、诊断控制和计时。
- [run.sh](./run.sh)：矩阵、rank 进程管理、日志和最终状态。
