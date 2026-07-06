# CCU Migration Notes

This directory contains the migrated CCU code collected from:

- `../hcomm`

The public CCU headers live in `include/ccu`. Migrated implementation files
are split by runtime responsibility instead of source repository:

- `src/ccu/dataplane/hcomm`: hcomm CCU primitive, REP, microcode, kernel,
  resource, and URMA-channel execution support migrated from `hcomm`.

The build produces one standalone data-plane shared-library target:

- `asccomm_ccu_dataplane`: CCU data-plane execution support, primitive C API,
  launch/token ABI, CCU kernel/template code, and device-side CCU headers. This
  target builds `libasccomm_ccu_dataplane.so`.

Control-plane entry points such as hcomm communicator/resource selection and
resource query remain in hcomm. `HcommCcuKernelRegister*`,
`HcommCcuKernelLaunch`, and `HcommCcuGetMemToken` are exported by this
data-plane SO.

Build checks:

```bash
cmake -S . -B build/ccu-check -DASCCOMM_BUILD_CCU=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/ccu-check --target asccomm_ccu_dataplane_compile_check -j 8
```

`asccomm_ccu_compile_check` and `asccomm_ccu_dataplane_compile_check` both
validate the standalone data-plane SO.
