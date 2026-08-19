# SIMT Notify/FAA/CAS样例

本样例用于验证SIMT URMA路径下的`WriteWithNotifyNbi`、`AtomicFAA`和`AtomicCAS`接口。rank 0负责
提交通信操作并校验本地fetch值和`Drain`状态，rank 1负责校验远端执行结果。

## 测试模式

- `notify`：提交一次Notify操作并调用`Drain`。
- `faa`：提交一次FAA操作，校验远端累加结果和本地取回的旧值。
- `cas`：提交一次CAS操作，校验远端交换结果和本地取回的旧值。
- `single`：串行提交Notify、FAA和CAS，每次操作后分别调用`Drain`。
- `batch_last`：Notify和FAA使用`commit=false`延迟提交，最后通过`commit=true`的CAS提交整个批次。
- `multi_lane`：三个lane分别以`commit=false`提交Notify、FAA和CAS；同步后由lane 0执行唯一一次
  `commit=true`的Notify并发布整个批次。
- `notify_immediate_repeat`：单lane连续提交100个`WriteWithNotifyNbi<true>`，每个WQE立即敲DB，最后统一调用一次`Drain`；用于覆盖连续立即提交场景。

同一channel允许多个lane并发执行`commit=false`；调用方同步后，只能由一个lane执行最终
`commit=true`并调用`Drain`。不支持多个lane同时执行`commit=true`。

## 编译

```bash
source /usr/local/Ascend/cann/set_env.sh
cd examples/simt_notify_atomic
bash build.sh
```

`build.sh`会先清理旧的`build`目录，再重新配置和编译样例，避免复用旧头文件或旧目标文件。

## 运行

样例通过`HcclGetRootInfo`和`HcclCommInitRootInfo`创建通信域，无需准备rank table。`run.sh`会为每次运行创建临时root info文件，由rank 0写入、其余rank读取：

```bash
bash run.sh 2 notify
bash run.sh 2 faa
bash run.sh 2 cas
bash run.sh 2 single
bash run.sh 2 batch_last
bash run.sh 2 multi_lane
bash run.sh 2 notify_immediate_repeat
```

每种模式的预期输出如下：

```text
RESULT | Mode=<mode> | Status=PASS
```

## 缓冲区下标约束

`LocalBufferAddr`和`RemoteBufferAddr`接收的是Channel缓冲区表下标，不是传给
`HcclChannelAcquire`的`memHandles`数组下标。本地表下标0由CCL buffer预留，因此本样例中第一个显式注册的
`sendBuf`使用下标1；远端`recvBuf`下标通过`HcclChannelGetRemoteMems`按tag查找。

本样例要求同一主机上至少有两张Ascend 950PR/950DT设备，并安装支持SIMT URMA接口的CANN版本。不同rank之间通过本地临时文件完成同步。
