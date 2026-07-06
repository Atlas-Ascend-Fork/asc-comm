# CCU Migration Notes

This directory contains the migrated CCU code collected from:

- `/mnt/workspace/gitCode/cann/hcomm`
- `/mnt/workspace/gitCode/cann/hccl`
- `/mnt/workspace/gitCode/cann/asc-devkit`

The public CCU headers live in `include/ccu`. Migrated implementation files
are split by runtime responsibility instead of source repository:

- `src/ccu/dataplane/hccl`: HCCL CCU dlsym wrappers, algorithm templates, and
  CCU kernel/template code migrated from `hccl`.
- `src/ccu/dataplane/device`: AscendC/HCCL device-side CCU implementation
  migrated from this repository's asc-devkit code.
- `src/ccu/dataplane/hcomm`: hcomm CCU primitive, REP, microcode, kernel,
  resource, and URMA-channel execution support migrated from `hcomm`.

The build produces one standalone data-plane shared-library target:

- `asccomm_ccu_dataplane`: hcomm CCU data-plane execution support, HCCL CCU
  dlsym wrappers, CCU algorithm templates, CCU kernel/template code, and
  device-side CCU headers. This target builds `libasccomm_ccu_dataplane.so`.

Control-plane entry points such as hcomm communicator/resource selection and
`HcommCcuKernelRegister*`/`HcommCcuKernelLaunch` remain in the original repos
and link to this data-plane SO.

Build checks:

```bash
cmake -S . -B build/ccu-check -DASCCOMM_BUILD_CCU=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/ccu-check --target asccomm_ccu_dataplane_compile_check -j 8
```

`asccomm_ccu_compile_check` and `asccomm_ccu_dataplane_compile_check` both
validate the standalone data-plane SO.
