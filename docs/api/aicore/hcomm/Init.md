# Init

## 功能说明

初始化 Hcomm 临时工作区。

## 函数原型

```cpp
__aicore__ inline int32_t Init(__ubuf__ uint8_t* buff, uint32_t len);

template <typename T>
__aicore__ inline int32_t Init(const AscendC::LocalTensor<T>& buff, uint32_t len);
```

## 参数说明

| 参数 | 输入/输出 | 说明 |
| --- | --- | --- |
| `buff` | 输入 | 调用方提供的 UB 缓冲区或 `LocalTensor`。 |
| `len` | 输入 | 缓冲区长度，单位为字节。 |

## 返回值

| 返回值 | 说明 |
| --- | --- |
| `0` | 初始化成功。 |
| `-1` | 初始化失败。 |

## 约束说明

- URMA 路径会使用 `buff` 作为临时工作区，起始地址需要满足 32 字节对齐。
- `len` 需要满足当前协议实现所需的最小工作区大小。
