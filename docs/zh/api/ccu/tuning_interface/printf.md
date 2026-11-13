# CCU_PRINTF

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

使用该接口需要包含头文件`ccu/hcomm/ccu_print.hpp`。

打印CCU kernel执行到调用位置时的`ccu::variable`值，用于调试。日志包含`[CCU_PRINT]`前缀、文件名、行号和格式化后的变量值。

打印正文使用与`HCCL_RUN_INFO`相同的INFO级别运行日志通道输出，遵循HCCL运行日志配置，可在运行日志文件中查看。

## 函数原型

```text
CCU_PRINTF(fmt, args...)
```

`args`表示零个或多个变量参数。文件名和行号由宏自动记录。

## 参数说明

| 参数名 | 输入/输出 | 描述 |
| --- | --- | --- |
| fmt | 输入 | 有效的、以`\0`结尾的Host格式字符串，构建时保存。不支持`nullptr`。 |
| args | 输入 | 零个或多个`AscendC::ccu::variable`，按设备运行时值输出。不支持Host整数、字符串、指针或浮点数。 |

| 格式符 | 含义 |
| --- | --- |
| `%u`、`%v` | 将变量的64位值按无符号十进制输出。 |
| `%x` | 按十六进制输出，不附加`0x`前缀。 |
| `%p` | 按带`0x`前缀的十六进制输出变量值，不解引用该地址。 |
| `%%` | 输出一个`%`，不消耗变量参数。 |

除`%%`外，格式符数量须与变量参数数量一致。支持无参数调用，如`CCU_PRINTF("reached dispatch")`。空字符串产生空正文，`CCU_PRINTF("%%")`输出一个`%`。

不支持`%d`、`%s`、`%f`、`%llu`以及宽度、精度、标志或位置参数。

## 返回值说明

返回`CcuResult`，表示Host侧打印指令的构建状态。

| 返回值/行为 | 说明 |
| --- | --- |
| `CCU_SUCCESS` | 打印指令构建成功。 |
| `CCU_E_PARA` | 格式字符串非法、格式符数量与参数数量不匹配，或注册失败导致打印句柄为空。 |
| 其他`CcuResult` | 构建内部`load`、`store`或条件控制指令失败，向调用方透传错误。 |
| 编译期错误 | 可变参数中包含非`ccu::variable`类型，触发静态类型检查。 |

格式不合法时，不分配打印缓冲区、不生成打印指令，返回`CCU_E_PARA`，并输出ERROR级别运行日志（`DLOG_ERROR`）：

```text
[CCU_PRINT] invalid format or argument count
```

Host处理记录时若检测到非法格式，输出WARNING日志，并停止本轮读取该打印点的记录。

| 非法输入 | 示例 |
| --- | --- |
| 空指针 | `CCU_PRINTF(nullptr)` |
| 未完成的格式符 | `"%"`、`"value=%u%"` |
| 不支持的格式符或长度修饰符 | `%d`、`%s`、`%f`、`%q`、`%llu` |
| 宽度、精度、标志或位置参数 | `%08x`、`%.2u`、`%+u`、`% u`、`%#x`、`%*u`、`%1$u` |
| 格式符与参数数量不匹配 | `CCU_PRINTF("%u")`，或向`""`、`"%%"`传入变量 |

资源分配等其他注册失败不生成打印指令，返回`CCU_E_PARA`。

## 约束说明

- 可以在`CCU_WHILE`等软件循环中调用，不可在硬件`ccu::loop`循环体中调用。
- 同一个打印点不支持多个mission并发执行。
- 格式字符串长度和变量参数数量受可用内存、CCU构建资源及日志组件的输出限制。
- 每个打印点最多缓存31条未被Host取走的记录。缓存满时丢弃新记录；Host取走记录后，释放的空间可用于后续打印。

> [!CAUTION]注意
> CCU_PRINTF接口打印功能会对CCU kernel实际运行的性能带来一定影响，通常在调测阶段使用。开发者可以按需在编译包含CCU_PRINTF调用的源文件时设置`-DASCENDC_DUMP=0`，关闭打印功能。

关闭打印时，CCU_PRINTF不求值参数、不分配打印缓冲区、不生成打印指令，返回`CCU_SUCCESS`。未定义ASCENDC_DUMP或其值非0时，打印功能开启。

## 使用说明

### 日志落盘

CCU_PRINTF正文沿用HCCL_RUN_INFO的INFO级运行日志通道，可在Host侧运行日志中查找`[CCU_PRINT]`。日志落盘路径通过[ASCEND_PROCESS_LOG_PATH](https://gitcode.com/cann/runtime/blob/master/docs/zh/env_vars/ASCEND_PROCESS_LOG_PATH.md)配置；日志分类、级别及相关配置参见[设置日志级别](https://gitcode.com/cann/runtime/blob/master/docs/zh/log_ref/setting_log_levels.md)。

### 调用示例

以下代码为kernel构建函数中的打印片段，`load_arg`参数0、1由Host launch传入：

```cpp
#include "ccu/hcomm/ccu_print.hpp"

namespace ccu = AscendC::ccu;

CcuResult BuildPrintBody()
{
    ccu::variable a, b, sum;
    CcuResult ret = ccu::load_arg(a, 0);
    if (ret != CCU_SUCCESS) {
        return ret;
    }
    ret = ccu::load_arg(b, 1);
    if (ret != CCU_SUCCESS) {
        return ret;
    }
    sum = a + b;
    return CCU_PRINTF("a=%u b=%u sum=%u hex=%x percent=%%", a, b, sum, sum);
}
```

当Host传入17和25时，运行日志中的打印正文为：

```text
a=17 b=25 sum=42 hex=2a percent=%
```
