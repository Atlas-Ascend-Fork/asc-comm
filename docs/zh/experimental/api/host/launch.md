# Kernel启动

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

通过`<<<schd, instance, stream>>>`语法启动CCU kernel。启动是异步的；返回成功只表示启动请求已提交，不表示设备执行已经完成。

## 调用形式

```cpp
status = kernel<<<schd, instance, stream>>>(task_arg0, task_arg1);
```

## 调度描述符类型

`HcomCcuSchd`定义在`<ccu_runtime.h>`中。

```cpp
typedef struct HcomCcuSchd {
  uint32_t missionNum;
  uint32_t missionStart;
  uint32_t rankId;
  uint32_t rankSize;
  uint64_t cacheTag;
  uint64_t channelSize;
  void *channelList;
} HcomCcuSchd;
```

## 调度描述符字段

| 字段 | 说明 |
| --- | --- |
| `missionNum` | 任务数量；当前仅支持1 |
| `missionStart` | 任务起始编号 |
| `rankId` | 当前rank ID，取值范围为0 <= rankId < rankSize |
| `rankSize` | 当前通信域rank数 |
| `cacheTag` | 预留字段，当前填`0` |
| `channelSize` | 显式传入channel时使用；未显式传入时填`0` |
| `channelList` | 显式传入channel时使用；未显式传入时填空指针 |

## 调度描述符构造

```cpp
HcomCcuSchd schd = {};
schd.missionNum = 1;
schd.missionStart = 0;
schd.rankId = rank;
schd.rankSize = world;
```

## 参数说明

| 项 | 输入/输出 | 类型 | 说明 |
| --- | --- | --- | --- |
| `schd` | 输入 | `HcomCcuSchd` | 调度描述符 |
| `instance` | 输入 | `ccu_instance_t` | HCOMM提供的instance句柄 |
| `stream` | 输入 | `void *` | 启动kernel任务的stream |
| 任务参数 | 输入 | kernel形参定义的类型 | 必须与kernel形参一致 |

## 返回值说明

| 返回值 | 说明 |
| --- | --- |
| `CCU_STATUS_SUCCESS` | 任务启动成功 |
| `CCU_STATUS_INVALID_ARGUMENT` | 参数非法 |
| `CCU_STATUS_SCHEMA_MISMATCH` | rank参数、mission参数或特例化kernel不匹配 |
| `CCU_STATUS_KERNEL_NOT_FOUND` | 找不到特例化kernel |
| `CCU_STATUS_CONTEXT_POISONED` | 当前实例已进入终止状态 |
| `CCU_STATUS_RUNTIME_ERROR` | 运行时启动失败 |

完整状态定义见[状态类型](status.md)。

## 约束说明

- 一个设备可能包含多个IO Die，不同IO Die包含不同的网络设备。实际组网中，不同的网络设备可能与不同的其他设备互联。一个CCU不能跨Die使用另一个IO Die的网络设备进行通信。
- 一个Kernel对应一个任务，当前单个Die仅支持一个任务，因此执行配置`schd`中的`missionNum`当前仅支持1。
- `schd.rankSize`必须等于kernel定义前`__ccu_rank_sizes__`声明的任意一个rankSize。
- 同一个instance在前一个kernel任务完成或失败清理完成前，不能再次启动新的kernel任务。
- `channelSize`/`channelList`只有在应用显式传递channel时才设置。

显式传递channel时，`channelList`指向channel句柄数组，`channelSize`为数组元素个数：

```cpp
uint64_t channels[2];
// channels由HCOMM接口获取。
HcomCcuSchd schd{};
schd.channelList = channels;
schd.channelSize = 2;
```

## 调用示例

```cpp
// 1. 构造调度描述符。
HcomCcuSchd schd{};
schd.missionNum = 1;
schd.missionStart = 0;
schd.rankId = rank;
schd.rankSize = world;

// 2. 异步启动 kernel。
ccu_status_t status = my_kernel<<<schd, instance, stream>>>(
    input.address, output.address, input.token, bytes);

// 3. 启动成功后，在stream上等待完成。
if (status == CCU_STATUS_SUCCESS)
  status = ccu_synchronize(instance, stream, 30000);
```
