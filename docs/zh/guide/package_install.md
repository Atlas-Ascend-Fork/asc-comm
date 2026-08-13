# 开发包构建与安装

先加载CANN环境：

```bash
source /usr/local/Ascend/cann/set_env.sh
```

构建开发run包：

```bash
bash scripts/package/build_package.sh
```

默认生成路径为`build_out/Ascend-asccomm_1.0.0_linux-<arch>.run`。设置包路径后，可以执行校验和安装：

```bash
RUN_PACKAGE=$(find build_out -maxdepth 1 -name 'Ascend-asccomm_1.0.0_linux-*.run' -print -quit)
chmod +x "${RUN_PACKAGE}"
"${RUN_PACKAGE}" --check
"${RUN_PACKAGE}" --full --install-path="${ASCEND_HOME_PATH}"
```

卸载开发包并恢复安装前的文件：

```bash
"${RUN_PACKAGE}" --uninstall --install-path="${ASCEND_HOME_PATH}"
```

安装和卸载命令需要由CANN安装目录的属主执行。
