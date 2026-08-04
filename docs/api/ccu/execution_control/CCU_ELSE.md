# CCU_ELSE

## 产品支持情况

- Ascend 950PR/Ascend 950DT：支持
- Atlas A3 训练系列产品/Atlas A3 推理系列产品：不支持
- Atlas A2 训练系列产品/Atlas A2 推理系列产品：不支持
- Atlas 200I/500 A2 推理产品：不支持
- Atlas 推理系列产品AI Core：不支持
- Atlas 推理系列产品Vector Core：不支持
- Atlas 训练系列产品：不支持
- Kirin X90：不支持
- Kirin 9030：不支持

## 功能说明

头文件为：`#include "include/ccu/ccu_control_flow_macro.h"`

抽象语义为：

$$body = \begin{cases}else, & paired\ CCU\_IF = false\\ \varnothing, & paired\ CCU\_IF = true\end{cases}$$

在CCU kernel内开始`CCU_IF`的else分支：当`CCU_IF`条件不满足时执行该分支。

`CCU_ELSE`是一个预处理器宏，必须紧跟在`CCU_IF { body }`之后使用，不能独立出现，与`CCU_IF`形成完整的if-else结构。

> [!NOTE]说明
> `CCU_ELSE`是可选的。`CCU_IF`可以单独使用，不搭配`CCU_ELSE`也是合法的。

## 函数原型

```cpp
CCU_IF(condExpr) {
    // then 分支 body
} CCU_ELSE {
    // else 分支 body
}
```

完整用法请参见[CCU_IF](CCU_IF.md)。

## 参数说明

`CCU_ELSE`无参数，与前一个`CCU_IF`自动配对，用户无需传入任何参数。

## 返回值说明

`CCU_ELSE`为预处理器宏，本身不返回`CcuResult`；在正常用法下不会失败。

## 约束说明

- `CCU_IF`只支持匹配一个`CCU_ELSE`，当前不支持单if多else的用法。
- `CCU_ELSE`必须紧跟在`CCU_IF { body }`之后，不可独立出现，不可出现在其他位置。
- `CCU_IF { body } CCU_ELSE { else-body }`之间不允许插入其他CCU API调用。

> [!CAUTION]注意
> 中间一旦插入任何CCU API调用（如数据搬运、同步等），框架会自动提前闭合该 `CCU_IF`；随后到达的`CCU_ELSE`找不到可配对的`CCU_IF`，其body会被跳过、注册仍然成功，运行期不会有任何错误返回值，极难调试。务必保持`CCU_IF { ... } CCU_ELSE { ... }`之间无任何CCU API调用。

- 不支持`else if`写法，如需多分支可嵌套`CCU_IF`：

```cpp
CCU_IF(v == 0) {
    // case 0
} CCU_ELSE {
    CCU_IF(v == 1) {
        // case 1
    } CCU_ELSE {
        // other case
    }
}
```

## 调用示例

```cpp
using namespace AscendC::ccu;

CcuResult MyKernel(CcuKernelArg arg) {
    Variable mode;
    LoadArg(mode, 0);

    CCU_IF(mode == 0) {
        // 模式0处理
        Variable result;
        result = 100;
    } CCU_ELSE {
        // 非模式0处理
        Variable result;
        result = 200;
    }

    return CCU_SUCCESS;
}
```
