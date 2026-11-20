# ccu_kernel_arg

## 功能说明

头文件为：`#include <ccu/asc_ccu.h>`

`ccu_kernel_arg`是CCU Kernel函数的编排上下文参数类型（`void*`），用于在`<<<>>>`直调Kernel时向Kernel函数体传递用户自定义参数（如Channel句柄、rank信息、预约句柄等）。

Kernel函数的固定最后一个形参类型为`ccu_kernel_arg`：Kernel函数体在翻译期执行时，将该形参转型为用户自定义的参数结构体指针读取内容。由此，`kernelArg`携带的值在翻译期即可见、可固化进指令（与taskArgs不同——taskArgs经`load_arg`声明，实际值在执行期才由硬件注入）。

## 类型定义

`ccu_kernel_arg`在`ccu/ccu_api/ccu_types.h`中定义：

```c
typedef void* ccu_kernel_arg;
```

## 用法说明

- Host侧定义参数结构体（如`struct CcuTransferKernelArg {...}`），填充内容后取指针作为`<<<>>>`直调的最后一个实参传入。
- Kernel函数体内将`ccu_kernel_arg`形参`static_cast`为该结构体指针后访问成员。
- 调用方须自行管理参数内存的生命周期：直调下发至流同步完成前参数内存须保持有效。

## 约束说明

- `ccu_kernel_arg`为`void*`类型，不做任何类型检查，结构体定义须Host侧与Kernel侧一致。
- 建议仅经`kernelArg`传递翻译期需要读取的编排信息；每次下发都变化的数据应经taskArgs（配合[load_arg](../device/arg_load_store/load_arg.md)）传递，以复用同一份已翻译的Kernel指令。

## 调用示例

```cpp
// Host侧：组装编排上下文并直调下发
// 为简化示例，省略错误处理；myRank、rankSize、inputAddr、schd、insHandle、stream
// 为用户上下文中的已有变量（分别来自通信域查询、任务参数、CCU实例查询与流创建）
#include <memory>

struct CcuTransferKernelArg {
    ChannelHandle channels[2]{};
    uint32_t rankId{0};
    uint32_t rankSize{0};
};
auto kernelArg = std::make_shared<CcuTransferKernelArg>();
kernelArg->rankId = myRank;
kernelArg->rankSize = rankSize;
CcuTransferKernel<<<schd, insHandle, stream>>>(inputAddr, kernelArg.get());
aclrtSynchronizeStream(stream);
```

```cpp
// Kernel侧：最后一个形参固定为ccu_kernel_arg，转型后读取（为简化示例，省略错误处理）
__ccu_host__ void CcuTransferKernel(uint64_t inputAddr, ccu_kernel_arg arg)
{
    auto* kernelArg = static_cast<CcuTransferKernelArg*>(arg);
    if (kernelArg == nullptr || kernelArg->rankSize != 2U) {
        return;
    }
    // 使用kernelArg->channels等成员...
}
```
