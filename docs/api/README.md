# API 参考

## AICore API

| 模块 | 说明 |
| --- | --- |
| [Hcomm](./aicore/hcomm/Hcomm.md) | AICore 侧点对点通信接口，提供 Read、Write、WriteWithNotify、Commit 和 Drain 等能力。 |

## 头文件

```cpp
#include "hcomm/hcomm.h"
```

使用该头文件前，需要确保编译 include path 包含仓库的 `include/aicore` 目录。
