# 28 · API · NCCL Parameter API

> 原文：NCCL API → NCCL Parameter API

这组函数和类型提供对 **NCCL 运行时参数**的访问。参数由**字符串 key** 标识（通常定义为环境变量、配置文件项，或通过 EnvPlugin 提供），可以按**类型化的值**读取，也可以按**字符串**读取。

两种访问风格：

| 风格 | 特点 |
| --- | --- |
| **Handle-based** | 用 `ncclParamBind()` 按 key 查一次并绑定到 `ncclParamHandle_t`，之后通过 handle 读类型化的值 |
| **Key-based** | 不用持久 handle，按 key 查找并以字符串读值。**每次访问都会重新查一遍 key** |

## Types

### ncclParamHandle_t

表示调用方与某个参数之间绑定关系的**不透明 handle**，由 `ncclParamBind()` 创建。

生命周期约定：

- handle 在 **NCCL 模块的整个生命周期内保持有效**。
- **调用方不得释放它**，也不应期望它在静态析构或进程拆除期间仍可访问。

## Handle-Based API

### ncclParamBind

```c
ncclResult_t ncclParamBind(ncclParamHandle_t* out, const char* key)
```

查找由 `key` 标识的参数，把它的 handle 存入 `*out`。

- 返回的 handle **由参数系统拥有，调用方不得释放**。
- `key` 是参数的规范名称（通常就是对应的环境变量名）。
- **若 `key` 不匹配任何已注册参数**，函数返回 `ncclInvalidArgument`，且 `*out` 保持不变。

```c
ncclResult_t ret = ncclSuccess;
ncclParamHandle_t myHandle = NULL;
ret = ncclParamBind(&myHandle, "NCCL_IB_ROCE_VERSION_NUM");
if (ret == ncclSuccess) {
  int32_t roceVerValue = 0;
  ret = ncclParamGetI32(myHandle, &roceVerValue);
  if (ret == ncclSuccess) {
    /* roceVerValue is set to the current value of NCCL_IB_ROCE_VERSION_NUM */
  }
}
```

### 类型化读取函数族

```c
ncclResult_t ncclParamGetI8 (ncclParamHandle_t h, int8_t*   out)
ncclResult_t ncclParamGetI16(ncclParamHandle_t h, int16_t*  out)
ncclResult_t ncclParamGetI32(ncclParamHandle_t h, int32_t*  out)
ncclResult_t ncclParamGetI64(ncclParamHandle_t h, int64_t*  out)
ncclResult_t ncclParamGetU8 (ncclParamHandle_t h, uint8_t*  out)
ncclResult_t ncclParamGetU16(ncclParamHandle_t h, uint16_t* out)
ncclResult_t ncclParamGetU32(ncclParamHandle_t h, uint32_t* out)
ncclResult_t ncclParamGetU64(ncclParamHandle_t h, uint64_t* out)
```

把绑定到 `h` 的参数值按类型 T 读出并存入 `*out`。覆盖有符号（I）和无符号（U）× 8/16/32/64 位的全部组合。

返回 `ncclInvalidArgument` 的两种情形：

- 参数值**无法转换**为请求的类型
- `h` 或 `out` 为 **NULL**

### ncclParamGetStr

```c
ncclResult_t ncclParamGetStr(ncclParamHandle_t h, const char** out)
```

返回绑定到 `h` 的参数的 C 风格字符串表示。

**生命周期陷阱**：写入 `*out` 的指针由参数系统拥有，**只在同一线程下次调用 `ncclParamGetStr()` 之前有效**。需要跨调用保留该值的调用方**必须自己拷贝**。

### ncclParamGet

```c
ncclResult_t ncclParamGet(ncclParamHandle_t h, void* out, int maxLen, int* len)
```

把绑定到 `h` 的参数的**原始二进制表示**拷贝到调用方分配的 buffer `out` 中。

- `maxLen` 是 `out` 的字节大小。
- 实际写入的字节数返回在 `*len`。
- **若参数的原生表示大于 `maxLen`**，函数返回 `ncclInvalidArgument`，且 `*len` 被设为**所需的大小**（可据此重新分配再调）。

## Key-Based API

### ncclParamGetParameter

```c
ncclResult_t ncclParamGetParameter(const char* key, const char** value, int* valueLen)
```

查找由 `key` 标识的参数，以 **null 结尾的字符串**返回其值。

- 写入 `*value` 的指针由参数系统拥有，**只在同一线程下次调用本函数之前有效**。
- `valueLen` 接收字符串的字节长度，**不含 null 终止符**。
- `key` 未找到时返回 `ncclInvalidArgument`。

```c
ncclResult_t ret = ncclSuccess;
const char* valueStr = NULL;
int len = 0;
ret = ncclParamGetParameter("NCCL_IB_ROCE_VERSION_NUM", &valueStr, &len);
if (ret == ncclSuccess) {
  printf("NCCL_IB_ROCE_VERSION_NUM=%s\n", valueStr);
}
```

### ncclParamGetAllParameterKeys

```c
ncclResult_t ncclParamGetAllParameterKeys(const char*** table, int* tableLen)
```

返回指向**所有已注册参数 key 的内部表**的指针。

- `*table` 被设为 `const char*` 数组，每个已注册参数一项；`*tableLen` 为项数。
- 该表及其中的字符串**由参数系统拥有**，两者都只在同一线程下次调用本函数之前有效。
- **返回表中的 key 未排序。**

**默认只返回在 Environment Variables 文档中公开的参数。** 设 `NCCL_PARAM_DUMP_ALL` 为 true 可强制返回**全部**参数（含私有参数）。

```c
ncclResult_t ret = ncclSuccess;
const char** keyTable = NULL;
int tableLen = 0;

ret = ncclParamGetAllParameterKeys(&keyTable, &tableLen);
if (ret == ncclSuccess) {
  for (int i = 0; i < tableLen; ++i) {
    printf("Key: %s\n", keyTable[i]);  /* prints all published parameter keys */
  }
}

putenv("NCCL_PARAM_DUMP_ALL=true");
ret = ncclParamGetAllParameterKeys(&keyTable, &tableLen);
if (ret == ncclSuccess) {
  for (int i = 0; i < tableLen; ++i) {
    printf("Key: %s\n", keyTable[i]);  /* prints all parameter keys */
  }
}
```

### ncclParamDumpAll

```c
void ncclParamDumpAll(void)
```

把**每个已注册参数的 key 和当前值**写到 NCCL 日志输出。用于诊断和配置审计。输出中的参数信息**未排序**。

同样：默认只输出公开参数，设 `NCCL_PARAM_DUMP_ALL=true` 可输出全部。

输出格式（`NCCL_PARAM_DUMP_ALL` 为 true 时的样例）：

```
NCCL_NO_CACHE (const char*) [Private, Cached] Comma-separated list of param keys to disable caching (or ALL)
  Current value=<unset> set_by=Default default=<unset>
  Accepted value: String

NCCL_PARAM_DUMP_ALL (bool) [Private] Print all parameters including private ones
  Current value=TRUE set_by=EnvPlugin default=FALSE
  Accepted value: Boolean: 1/T/TRUE or 0/F/FALSE
```

可以看到每项包含：参数名、类型、属性标记（如 `Private`、`Cached`）、描述，以及**当前值、由谁设置（`set_by`）、默认值**和可接受的取值形式。`set_by` 对排查"这个参数到底被谁改了"很有用。

## 两种风格的选择

| 场景 | 建议 |
| --- | --- |
| 反复读同一个参数 | Handle-based：`ncclParamBind` 一次，之后用类型化 getter |
| 一次性读取、或只需字符串 | Key-based：`ncclParamGetParameter` |
| 枚举/审计全部参数 | `ncclParamGetAllParameterKeys` 或 `ncclParamDumpAll` |

注意所有返回内部指针的函数（`ncclParamGetStr`、`ncclParamGetParameter`、`ncclParamGetAllParameterKeys`）都是**同一线程下次调用即失效**，需要保留就拷贝。
