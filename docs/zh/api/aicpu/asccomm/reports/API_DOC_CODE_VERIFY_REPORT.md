# AICPU Asccomm API文档-代码一致性校验报告

## 1. 检查范围

- 检查对象：23个AICPU Asccomm API页面。
- 参考skill：`asc-api-doc-verify`。
- 代码依据：
  - `asc-comm_pd/include/aicpu/hcomm/asccomm_primitives.h`
  - `asc-comm_pd/src/aicpu/asccomm/primitives/api_c_adpt/aicpu_ts_asccomm_primitives_c_adpt.cc`
  - `asc-comm_pd/src/aicpu/asccomm/primitives/launch/asccomm_launch.cc`
  - `asc-comm_pd/src/aicpu/asccomm/resource/common/asccomm_runtime.cc`
  - `asc-comm_pd/src/aicpu/asccomm/resource/common/asccomm_transport.cc`
- 检查时间：2026-09-30 15:30 Asia/Shanghai。
- 匹配置信度：HIGH。所有API均来自同一个公开头文件，且函数名与文档页面一一对应。

## 2. 总体结论

23个API的函数名、参数个数、参数名、参数类型、返回值类型均与公开头文件完全一致，未发现ERROR级签名不一致。

文档中的主要行为约束已按当前实现更新。仍存在3类WARNING级外部证据问题：产品支持矩阵、芯片/协议组合、以及Channel Notify Wait索引布局。这些问题无法仅凭数据面代码确认，已记录到遗留问题文档。

**Summary: 0 errors, 3 warnings, 3 info**

## 3. 签名一致性

| 检查项 | 结果 |
| --- | --- |
| API数量 | 通过：文档23个，公开头文件声明23个。 |
| 函数名 | 通过：23个全部精确一致。 |
| 参数个数 | 通过：23个全部一致。 |
| 参数名 | 通过：23个全部一致。 |
| 参数类型 | 通过：规范化空格和指针写法后一致。 |
| 返回值类型 | 通过：均为`int32_t`。 |
| 头文件引用 | 通过：安装路径统一为`"asccomm/asccomm_primitives.h"`。 |
| 类型来源 | 通过：公开头文件包含`hcomm_res_defs.h`，文档引用`"asccomm/hcomm_res_defs.h"`。 |

## 4. 实现一致性核对

| API/接口组 | 文档描述 | 代码行为 | 结论 |
| --- | --- | --- | --- |
| `asccomm_batch_mode_start/end` | `batchTag`为保留参数，当前不读取、不校验。 | 两个入口均不读取`batchTag`。 | 一致。 |
| 批量模式生命周期 | 批量状态为当前执行线程thread-local状态。 | `LaunchState`为`thread_local`。 | 一致。 |
| 批量提交 | 本地SQE缓存达到128条时可能提前提交。 | `HCOMM_RESOURCE_RTSQ_BATCH_SIZE`为128。 | 一致。 |
| `asccomm_local_reduce_on_thread` | `prod`不支持。 | SDMA reduce映射中`PROD`无硬件操作。 | 一致。 |
| 远端归约写 | RoCE inline归约`prod`不支持。 | `Roce3ReduceOpTypeMap[PROD]`为无效。 | 一致。 |
| `asccomm_read_reduce_on_thread` | RoCE通道不执行数据操作，直接返回成功。 | RoCE分支直接返回`ASCCOMM_SUCCESS`。 | 一致。 |
| `asccomm_channel_drain_on_thread` | RoCE通道直接返回成功。 | RoCE分支直接返回`ASCCOMM_SUCCESS`。 | 一致。 |
| `asccomm_batch_transfer_on_thread` | 仅支持UB类协议，RoCE返回不支持。 | 先调用`get_ub_context()`，RoCE返回不支持。 | 一致。 |
| 超时设置 | 非负小数向零截断，负数/NaN/超上限报参数错误。 | `convert_timeout()`行为一致。 | 一致。 |
| 默认Notify超时 | 未设置时1836秒。 | `DEFAULT_NOTIFY_WAIT_TIMEOUT=1836`。 | 一致。 |
| 默认RTSQ满超时 | 未设置时1856秒。 | `DEFAULT_SQ_FULL_TIMEOUT=1856`。 | 一致。 |
| Thread句柄 | 当前实现要求有效且不能为0。 | `get_thread_entity()`首先检查`thread==0`。 | 一致。 |
| Channel句柄 | 当前实现要求有效且不能为0。 | `get_context()`首先检查`channel==0`。 | 一致。 |
| 公开错误码 | 文档使用`ASCCOMM_*`错误码。 | 实现使用`AsccommResult`。 | 一致。 |

## 5. WARNING项

### WARNING-1：产品支持矩阵缺少代码外证据

- **文档**：23个页面均包含产品支持情况。
- **代码**：数据面实现没有按芯片型号分支，无法推导产品支持状态。
- **建议**：由产品支持矩阵或硬件验证结果确认后更新。

### WARNING-2：芯片/引擎/协议组合缺少控制面证据

- **文档**：当前只描述数据面可确认的协议行为，不承诺芯片和引擎组合。
- **代码**：实现按协议分支，不按芯片分支。
- **建议**：控制面建链能力和产品矩阵确认后补充推荐组合。

### WARNING-3：Channel Notify Wait索引布局需要控制面确认

- **文档**：`localNotifyIdx`须小于Channel资源中可用于等待的本地Notify数量。
- **代码**：检查`localNotifyNum - 1`并访问`localNotifyAddr[localNotifyIdx + 1]`。
- **建议**：确认控制面填充数组时是否包含保留项，排除off-by-one。

## 6. INFO项

1. 4GiB分片边界需要运行时或驱动侧确认；文档已避免承诺边界行为。
2. `batchTag`当前为保留参数，后续是否实现真实语义待产品确认。
3. `AsccommResult`完整错误码表是否作为公开资料发布待确认。

## 7. 结论

文档与最新代码的内容一致性通过。签名层面没有问题；行为描述已对齐当前实现。发布前需闭环产品矩阵、芯片/协议组合和Channel Notify Wait索引布局三个外部证据问题。
