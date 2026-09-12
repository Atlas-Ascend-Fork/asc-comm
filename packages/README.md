# Local Build Packages (2026-09-12)

Offline-built run packages based on PR #113 latest content (`ccu_rename` @ e5e9233) plus the
sample-header decoupling fix, from the `ccu_rename_cbb` branches of asc-devkit / hcomm / asc-comm.

## Packages

| File | Source repo / commit | SHA256 |
|---|---|---|
| `cann-asc-devkit_9.2.0_linux-aarch64.run` | qq_39172959/asc-devkit@ccu_rename_cbb (7eb168559), 联编同级 asc-comm@e5e9233+47fb737 | `d1fe030e507ded7e3b9c4b776eb2d45c02f8960e27ad8a4b35d445cb513cbf34` |
| `cann-hcomm_9.2.0_linux-aarch64.run` | qq_39172959/hcomm@ccu_rename_cbb (472ec6acb) | `65d0e1ddfda660147a9b00672b7055431e24f5f8f7cea5491e64dcaeedeb5250` |

asc-comm 的内容（CCU 数据面 `libasccomm_ccu_dataplane.so` + `include/ccu/hcomm` DSL 头）已包含在 asc-devkit 包内，无需单独安装 asc-comm 包。

## Build environment

- Toolkit: Ascend-cann-toolkit 9.2.0 (aarch64, OBS 20260729 build), installed at `/home/developer/Ascend/cann-9.2.0`
- Build type: Release, aarch64 本机编译
- asc-comm 基线: `ccu_rename_cbb` 分支 = PR #113 最新内容 (e5e9233) + 样例头文件解依赖修复 (47fb737)

## Install order

```
bash cann-asc-devkit_9.2.0_linux-aarch64.run --install
bash cann-hcomm_9.2.0_linux-aarch64.run --install
```

## Notes / caveats

1. **hcomm 包内含 `libstub_libaclrt.so`（本地兜底桩库）**：当前公开 CANN 9.2.0 toolkit（20260729 构建）的 `libruntime.so` 尚未导出 `aclrtExceptionInfoCallbackRegister/Unregister` 公共符号，而 hcomm `ccu_rename_cbb` 分支已改为直接调用新接口且开启 `--no-undefined` 链接校验。桩库满足链接校验；在新版 runtime（导出该符号）环境上安装时，可删除 lib64 下的 `libstub_libaclrt.so`。
2. devkit 包基于升级后的 9.2.0 toolkit 完整重编，包含 asc-comm CCU 数据面 SO 与公开 DSL 头（`include/ccu/hcomm`）。
3. **hcomm 包已包含 `opp/built-in/op_impl/aicpu/config/ccl_kernel.json`**（由本仓 `ccl_kernel.ini` 生成，46 个 AICPU op 条目，与 CANN 自带旧版 json 条目集不同）。host 侧通信域初始化（`LoadBinaryFromFile`）依赖该文件；若目标机 CANN 自带旧版，安装本包会覆盖为新版。
