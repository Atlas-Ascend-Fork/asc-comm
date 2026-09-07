# simt_jetty

SIMT 侧 Jetty / DWQE 样例，`thread / warp / group` 三种协作方式都在这里验证。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh
```

没有硬件时，可在编译阶段检查 SIMT VF 的栈和寄存器占用：

```bash
CCE_RES_USAGE=1 bash build.sh
```

## 执行

```bash
bash run.sh 2
```

`nranks` 必须大于等于 2。样例只实际使用 rank 0 和 rank 1，其余 rank 会直接退出。
