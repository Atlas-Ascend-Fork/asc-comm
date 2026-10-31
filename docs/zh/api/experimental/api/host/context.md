# Context与资源

## 产品支持情况

<!-- npu="950" id1 -->
- Ascend 950PR&950DT系列产品：支持
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3系列产品：不支持
<!-- end id2 -->
<!-- npu="910b" id3 -->
- Atlas A2系列产品：不支持
<!-- end id3 -->
<!-- npu="310b" id4 -->
- Atlas 200I/500 A2推理产品：不支持
<!-- end id4 -->
<!-- npu="310p" id5 -->
- Atlas推理系列产品：不支持
<!-- end id5 -->
<!-- npu="910" id6 -->
- Atlas训练系列产品：不支持
<!-- end id6 -->

## 功能说明

创建、绑定和销毁CCU runtime context。context借用外部communicator与instance，不接管它们的生命周期。

## 函数原型

```cpp
ccu_status_t ccu_context_create(const ccu_context_options_t *options,
                                ccu_context_t **context);

ccu_status_t ccu_context_bind_instance(ccu_context_t *context,
                                        ccu_instance_t *instance);

ccu_status_t ccu_context_destroy(ccu_context_t **context);
```

## 参数说明

### `ccu_context_create`

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `options` | 输入 | `const ccu_context_options_t *` | context配置；`struct_size`和`api_version`必须匹配当前runtime |
| `context` | 输出 | `ccu_context_t **` | 成功时返回新context |

### `ccu_context_bind_instance`

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `context` | 输入 | `ccu_context_t *` | 已创建的context |
| `instance` | 输入/输出 | `ccu_instance_t *` | 部署环境提供的instance句柄；传入`0`时runtime会生成并写回本context可用的instance；绑定后供launch使用 |

### `ccu_context_destroy`

| 参数 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `context` | 输入/输出 | `ccu_context_t **` | 指向待销毁context的指针；成功后置空 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `CCU_STATUS_SUCCESS` | 成功 |
| `CCU_STATUS_INVALID_ARGUMENT` | 参数为空、版本不匹配或结构体大小不匹配 |
| `CCU_STATUS_INVALID_OBJECT` | 部署环境返回非法对象 |
| `CCU_STATUS_BUSY` | instance冲突或mission未能完成并释放 |
| `CCU_STATUS_RUNTIME_ERROR` | 运行时内部错误 |
| `CCU_STATUS_VERSION_MISMATCH` | 编译产物ABI版本不匹配 |
| `CCU_STATUS_RESOURCE_EXHAUSTED` | 运行时资源不足 |

## Context配置

```cpp
typedef struct ccu_context_options_t {
  uint32_t struct_size;
  uint32_t api_version;
  void *communicator;
  uint64_t default_sync_timeout_ms;
  uint64_t abort_timeout_ms;
  uint64_t reserved[4];
} ccu_context_options_t;
```

- `struct_size`和`api_version`必须使用当前SDK提供的值。
- `communicator`由部署环境创建，context只借用，不接管所有权。
- `default_sync_timeout_ms`是`ccu_synchronize`传入`0`时使用的默认等待时间；必须为非零值。
- `abort_timeout_ms`是超时后执行恢复动作的等待时间；必须为非零值。
- `reserved`必须保持零值。

## Abort策略

```cpp
typedef enum ccu_abort_action_t {
  CCU_ABORT_NONE = 0,
  CCU_ABORT_STREAM_ONLY = 1,
  CCU_ABORT_DEVICE = 2
} ccu_abort_action_t;
```

当前runtime负责执行超时后的恢复动作。用户资料只需遵守：同步必须使用有限超时；发生超时后按返回状态进入cleanup，不要继续读取输出或追加mission。

## Buffer view

```cpp
typedef struct ccu_buffer_view_t {
  uint64_t address;
  uint64_t size;
  uint64_t token;
} ccu_buffer_view_t;
```

`address`、`size`和`token`必须来自同一段已准备好的通信内存。token是HCOMM/Host侧颁发的内存访问凭证，不能由地址推导，也不能混用不同buffer的token。

## 约束说明

- context借用communicator，不接管communicator的所有权。
- 创建失败后不能提交kernel。
- instance在launch和synchronize完成前不能销毁或替换。
- `ccu_context_destroy`对已经置空的context指针是幂等的。
- 销毁context前，应先让mission完成或进入terminal状态；destroy会尽力清理仍保留的mission。
- 成功和失败早退都必须进入同一cleanup路径。
- `ccu_status_string`返回稳定的状态文本；未知状态返回`unknown status`。

## 调用示例

```cpp
// 1. 初始化 context 配置。
ccu_context_options_t options = {};
options.struct_size = sizeof(options);
options.api_version = CCU_RUNTIME_API_VERSION;
options.communicator = communicator;
options.default_sync_timeout_ms = 30000;
options.abort_timeout_ms = 10000;

// 2. 创建 context。
ccu_context_t *context = nullptr;
ccu_status_t status = ccu_context_create(&options, &context);
if (status != CCU_STATUS_SUCCESS)
  return status;

// 3. 绑定部署环境提供的 instance，供 launch 使用。
status = ccu_context_bind_instance(context, &instance);
```
