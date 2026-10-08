# 简介

`variable`在注册阶段仅为标量占位符，自身不携带有效值，必须借助本节接口才能和外部数据建立关联。为此，本节提供一组接口，实现CCU Kernel内部`variable`和外部标量值之间的读写：支持把外部数据加载到`variable`，也支持将`variable`的值存储到外部。

## 接口列表

- [load_arg](load_arg.md)：在注册阶段完成声明，在执行阶段从Host Launch注入的`taskArgs`中加载参数。
