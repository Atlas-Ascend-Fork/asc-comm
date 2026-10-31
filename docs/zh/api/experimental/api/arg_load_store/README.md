# 参数加载/存储

CCU kernel的显式形参由Host launch时的参数提供。Host wrapper按kernel形参顺序装箱参数；kernel内部可以使用变量加载/存储接口读写带token的标量值。

## 接口列表

- [变量加载与存储](load_store_variable.md)：`__ccu_load_variable`和`__ccu_store_variable`

当前公开接口面没有单独的Host参数加载函数；参数绑定由生成的kernel wrapper完成。
