# SIMT Notify/FAA/CAS样例

本样例用于验证SIMT URMA路径下的`WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`接口。rank 0负责提交通信操作，rank 1负责校验远端执行结果。

## 测试模式

- `notify`：提交一次Notify操作并调用`Drain`。
- `faa`：提交一次FAA操作，校验远端累加结果和本地取回的旧值。
- `cas`：提交一次CAS操作，校验远端交换结果和本地取回的旧值。
- `single`：串行提交Notify、FAA和CAS，每次操作后分别调用`Drain`。
- `batch_last`：Notify和FAA使用`commit=false`延迟提交，最后通过`commit=true`的CAS提交整个批次。
- `multi_lane`：三个lane分别延迟提交Notify、FAA和CAS；同步后由lane 0追加一次Notify并提交整个批次。

## 准备开发验证环境

样例编译时使用已安装到CANN目录的asc-comm头文件。先在仓库根目录生成并安装开发验证run包：

```bash
source /usr/local/Ascend/cann/set_env.sh
bash build.sh --pkg
./build_out/Ascend-asccomm_1.0.0_linux-<arch>.run --full
```

如果需要指定CANN目录，可增加`--install-path=/path/to/cann`。完整参数参见
[`docs/zh/guide/build_and_test.md`](../../docs/zh/guide/build_and_test.md)。

## 编译

```bash
cd examples/simt_notify_atomic
bash build.sh
```

`build.sh`会先清理旧的`build`目录，再重新配置和编译样例，避免复用旧头文件或旧目标文件。

## 运行

运行方式与已有的`simt_write`样例保持一致，需要传入HCCL rank table：

```bash
bash run.sh 2 /path/to/rank_table.json notify
bash run.sh 2 /path/to/rank_table.json faa
bash run.sh 2 /path/to/rank_table.json cas
bash run.sh 2 /path/to/rank_table.json single
bash run.sh 2 /path/to/rank_table.json batch_last
bash run.sh 2 /path/to/rank_table.json multi_lane
```

每种模式的预期输出如下：

```text
[rank 0] simt_notify_atomic <mode> PASS
[rank 1] simt_notify_atomic <mode> PASS
```

## 缓冲区下标约束

`LocalBufferAddr`和`RemoteBufferAddr`接收的是Channel缓冲区表下标，不是传给
`HcclChannelAcquire`的`memHandles`数组下标。本地表下标0由CCL buffer预留，因此本样例中第一个显式注册的
`sendBuf`使用下标1；远端`recvBuf`下标通过`HcclChannelGetRemoteMems`按tag查找。

本样例要求同一主机上至少有两张Ascend 950PR/950DT设备，并安装支持SIMT URMA接口的CANN版本。不同rank之间通过本地临时文件完成同步。
