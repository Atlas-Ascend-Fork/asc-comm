# AICPU Asccomm API资料遗留问题

本文记录以`asc-comm_pd`当前实现为依据更新API资料后，仍需要产品、控制面或硬件侧确认的问题。资料中已按当前实现描述可确认行为；下列内容暂不作为用户约束写入各API页面。

## 1. 产品支持矩阵

当前23个API页面的“产品支持情况”沿用hcomm资料迁移后的模板，未在`asc-comm_pd`数据面实现中找到对应芯片分支或统一支持矩阵。以下问题需要产品支持矩阵确认：

- `asccomm_aclrt_notify_record_on_thread`和`asccomm_aclrt_notify_wait_on_thread`是否确实为Ascend 950PR/Ascend 950DT不支持、Atlas A3/A2支持。
- `asccomm_channel_fence_on_thread`是否确实为Ascend 950PR/Ascend 950DT支持、Atlas A3/A2不支持。
- `asccomm_write_with_notify_on_thread`和`asccomm_write_reduce_with_notify_on_thread`是否确实为Ascend 950PR/Ascend 950DT支持、Atlas A3/A2不支持。
- `asccomm_batch_transfer_on_thread`在Ascend 950PR/Ascend 950DT、Atlas A3/A2上的实际支持状态需要结合控制面建链和运行验证确认。
- 当前实现中的RTSQ、UB和RoCE路径是否对所有标为支持的芯片均可运行，需要硬件验证结果确认。

确认后需要统一更新23个API页面开头的“产品支持情况”和芯片过滤标签。

## 2. 芯片与协议组合

当前实现按协议分支处理，不按芯片型号分支。原hcomm资料中存在大量“某芯片仅支持某引擎/协议”的约束，例如AICPU_TS、CPU_TS、RoCE、UB_CTP、UBoE等组合。`asc-comm_pd`数据面源码无法证明这些组合的芯片适用性。

需要控制面建链能力和产品支持矩阵确认以下事项：

- Thread资源在AICPU_TS、CPU_TS等引擎下如何构造，以及与芯片型号的对应关系。
- Channel资源在RoCE和UB类协议下的可用芯片范围。
- Host CPU侧调用是否属于当前AICPU Asccomm公开使用模型；若支持，控制面如何构造Thread和Channel资源。
- 各接口在不同芯片上的推荐引擎和协议组合。

确认前，API页面仅描述当前实现可确认的协议行为，不承诺芯片和引擎组合。

## 3. Channel Notify Wait索引布局

`asccomm_notify_wait()`当前实现检查：

```cpp
localNotifyIdx >= channel.localNotifyNum - 1U
```

并访问：

```cpp
channel.localNotifyAddr[localNotifyIdx + 1U]
```

这意味着实际可用于等待的索引范围取决于`localNotifyNum - 1`，且实现会跳过本地Notify数组的第0项。需要控制面确认：

1. `localNotifyNum`是否包含一个保留项或内部drain资源；
2. 用户可见的`notifyNum`与`localNotifyNum - 1`的映射关系；
3. 当前实现是否存在off-by-one问题。

当前文档已将`localNotifyIdx`描述为“小于Channel资源中可用于等待的本地Notify数量”，但未直接承诺等于控制面创建时的`notifyNum`。

## 4. 4GiB分片边界

`asccomm_local_copy_on_thread()`和`asccomm_local_reduce_on_thread()`当前按`0x100000000ULL`（4GiB）分片，但底层SDMA函数的`size`参数类型为`uint32_t`，随后执行整数转换。单片恰好为4GiB时可能转换为0，导致边界行为不确定。

需要运行时或驱动侧确认：

- 恰好4GiB、4GiB整数倍和大于4GiB的请求是否可正确分片；
- 若不支持该边界，实现应将单片上限改为小于4GiB，或在文档中明确长度上限。

## 5. 归约支持范围

当前公开枚举和适配层参数转换接受`HCOMM_REDUCE_PROD`，但底层路径不支持：

- 本地SDMA归约：`PROD`返回`ASCCOMM_E_NOT_SUPPORT`。
- RoCE inline归约：`PROD`映射为无效硬件操作，返回`ASCCOMM_E_NOT_SUPPORT`。

因此文档已将归约操作描述为`sum、max、min`，并补充`PROD`的失败行为。需要产品侧确认：

1. `PROD`是否计划支持；
2. 如果长期不支持，是否应在适配层前置校验中直接拒绝；
3. 各芯片下UB与RoCE归约的数据类型支持矩阵是否与当前文档中的按芯片数据类型列表一致。

## 6. RoCE特殊行为

以下RoCE行为来自当前实现，需要产品侧确认是否为长期公开语义：

- `asccomm_read_reduce_on_thread()`在RoCE通道上不执行数据操作，直接返回成功。
- `asccomm_channel_drain_on_thread()`在RoCE通道上不执行额外等待，直接返回成功。

如果这些行为是迁移过程中的临时实现，应在代码修复后更新文档；如果是长期语义，建议在总览资料中更显著地说明，避免用户误以为调用成功等价于数据已更新或任务已完成。

## 7. `batchTag`参数语义

`asccomm_batch_mode_start()`和`asccomm_batch_mode_end()`的`batchTag`参数当前不被读取或校验。文档已将该参数描述为保留参数。

需要确认：

1. 该参数是否计划实现批量任务标识或缓存管理；
2. 如果是纯兼容保留参数，是否应在公开头文件或接口说明中标注；
3. 如果后续实现该语义，需同步更新参数说明、约束和调用示例。

## 8. End失败后的重试语义

`asccomm_batch_mode_end()`当前实现会先结束批量模式，再提交已登记Thread的缓存任务；提交失败时提前返回，不会清空已登记的Thread列表。因此，失败后是否可以再次调用本接口重试，以及重试时会提交哪些任务，当前代码和测试未明确定义。

需要代码负责人或产品侧确认失败后的推荐处理方式；确认前，API页面只描述成功路径和当前可确认的失败返回，不承诺失败后可重试。

## 9. Timeout默认值的外部偏移

当前`asc-comm_pd`实现中：

- Notify等待默认值为1836秒。
- RTSQ队列满等待默认值为1856秒。

原hcomm资料提到Host侧默认值为`1836+27秒`、非AICPU模式增加27秒安全偏移，但当前`asc-comm_pd`数据面代码中没有该逻辑。需要底层运行时或旧hcomm实现确认该偏移是否仍存在。

确认前，文档仅描述当前可确认的1836秒和1856秒默认值。

## 10. 错误码公开契约

当前公开函数返回`int32_t`，内部使用`AsccommResult`。文档已使用`ASCCOMM_SUCCESS`、`ASCCOMM_E_PARA`、`ASCCOMM_E_PTR`、`ASCCOMM_E_NOT_SUPPORT`、`ASCCOMM_E_INTERNAL`等错误码描述失败。

当前各接口的返回值说明主要依据`asc-comm_pd`代码路径整理，尚未完成逐项人工校验和交叉核对。后续需要结合测试结果、运行时行为和产品约定复核返回值的可达性及含义。

需要确认：

1. `AsccommResult`完整错误码表是否作为公开资料发布；
2. 每个API的常见错误码是否需要逐项列表化；
3. 底层驱动、RoCE和UB操作返回的其他错误码是否需要透传给用户说明。

## 11. 控制面接口文档

当前AICPU数据面文档中的`ThreadHandle`和`ChannelHandle`由控制面构造并随Kernel参数下发，但asc-comm仓尚未提供对应的AICPU控制面资源创建和使用文档。

需要确认：

1. 控制面资源创建接口属于asc-comm文档范围还是继续引用hcomm文档；
2. 如果继续引用hcomm，需要确定稳定的外部链接和版本；
3. 如果迁移到asc-comm，需要补充对应控制面API页面后再从数据面页面链接。

## 12. 单边通信内存接口本端地址校验

`asccomm_read_on_thread()`、`asccomm_read_reduce_on_thread()`和`asccomm_write_on_thread()`在UB和RoCE路径中查找本端通信内存缓冲区时允许使用第一个本地通信内存缓冲区作为保护信息兜底，即使本端地址范围未命中本地已注册缓冲区，见`asccomm_transport.cc`中的`find_buffer(..., true)`调用。远端地址则必须命中远端已注册通信内存缓冲区。

需要确认：

1. 本端地址未命中注册范围时的兜底行为是否为兼容实现，还是应由代码改为直接返回错误；
2. API文档是否应长期要求`dst`严格落在本端已注册通信内存范围内；
3. 如果该行为保留，底层使用第一个缓冲区保护信息访问范围外地址是否有额外运行时保证。

## 13. `asccomm_read_reduce_on_thread()`归约参数支持范围

`asccomm_read_reduce_on_thread()`的适配层可以转换比UB归约指令编码更多的`dataType`和`reduceOp`取值，但UB编码当前仅处理有限的数据类型以及`SUM`、`MAX`、`MIN`。适配层接受、但UB编码不支持的组合可能在任务生成阶段抛出异常，最终返回`ASCCOMM_E_INTERNAL`。RoCE路径则在参数转换成功后直接返回`ASCCOMM_SUCCESS`，不执行归约读。

需要确认：

1. 归约读公开支持的数据类型和操作范围是否应由适配层前置限制；
2. UB不支持的组合是否应返回`ASCCOMM_E_NOT_SUPPORT`或`ASCCOMM_E_PARA`；
3. RoCE归约读直接成功但不执行操作是否为长期公开语义。
