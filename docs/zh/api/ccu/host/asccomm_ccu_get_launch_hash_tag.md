# asccomm_ccu_get_launch_hash_tag

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

头文件为：`#include <ccu/asc_ccu.h>`

将任意标签字符串哈希为`uint64_t`，传入`nullptr`时返回0。返回值用作Kernel下发配置`asccomm_ccu_schd`中`binary_cache_tag`字段的取值，详见[CCU编程模型](../../../guide/programming_model/ccu_programming.md)核函数一节。

## 函数原型

```c
uint64_t asccomm_ccu_get_launch_hash_tag(const char *tag);
```

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| tag | 输入 | 标签字符串指针，内容任意。为避免句柄错误复用，标签需保证唯一性，详见约束说明。 |

## 返回值说明

| 情形 | 返回值 |
| --- | --- |
| `tag`非空 | 标签字符串的哈希值（`uint64_t`）。相同字符串返回相同值，填入`binary_cache_tag`后启用Kernel句柄缓存。 |
| `tag == nullptr` | 0，表示不启用Kernel句柄缓存。 |

## 约束说明

- 相同标签必须对应同一个Kernel：命中缓存后不再执行Kernel函数体，不同Kernel共用标签（同一实例、同一Die下）会复用错误句柄。
- 多个rank共用进程或CCU实例时，标签需包含rank等区分维度，避免不同rank的Kernel错误复用。

## 调用示例

```cpp
// 标签按rankId生成，保证各rank标签唯一
uint32_t rankId = 0; // rankId为当前rank在通信域中的编号
const std::string launchTag = "CcuAllGatherMesh1DMem2MemKernel_rank_" + std::to_string(rankId);
uint64_t tag = asccomm_ccu_get_launch_hash_tag(launchTag.c_str());
```
