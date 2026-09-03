# Batch性能临时对比用例

该用例在一个AICore上测试Write、Read和WriteWithNotify：

- `simd`：一个Jetty，SIMD串行Append一批WQE后调用一次BatchCommit。
- `multi-jetty`：2/4/8个独立Jetty，SIMT为每个Jetty建立一个Lane子组并行提交。

每个Jetty默认执行1024项正式操作，数据量为64B。BatchSize与每个Jetty的Lane数相同，测试
`1/8/32/64/128/256`。当`Jettys × BatchSize`不超过1024时，全部子组同时执行；`8 × 256`
使用同一核上的两波4-Jetty子组。

`IssueTime/ns`等于一个核完成全部Jetty下发所用cycle除以`Iterations × Jettys`。SIMD单Jetty
路径中的Jettys为1。该时间包含Append和BatchCommit，不包含Drain。

```bash
bash build.sh --clean
bash run.sh simd
bash run.sh multi-jetty
bash run.sh all
```

可用`--api write|read|notify|all`缩小范围，例如：

```bash
bash run.sh multi-jetty --api write --batch-sizes 32,128 --jetty-counts 2,8
```

每行结果只保留Path、API、DataSize、BatchSize、Lanes、Cores、Waves、Jettys、DBCount、
Bandwidth、IssueTime和Status。
