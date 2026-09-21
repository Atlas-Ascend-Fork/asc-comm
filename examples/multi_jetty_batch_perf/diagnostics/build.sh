#!/usr/bin/env bash
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
clean_build=false
fast_build=OFF
backend=all
scope=none
matrix_batch=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            clean_build=true
            shift
            ;;
        --fast)
            fast_build=ON
            shift
            ;;
        --backend)
            [[ $# -ge 2 ]] || { echo "--backend requires all, simt, or simd" >&2; exit 1; }
            backend="$2"
            shift 2
            ;;
        --matrix-batch)
            echo "Current matrix: ../build.sh --batch-size N" >&2; exit 1
            [[ $# -ge 2 && "$2" =~ ^(1|8|32|64|128|256)$ ]] || { echo "--matrix-batch requires 1,8,32,64,128,256" >&2; exit 1; }
            matrix_batch="$2"
            shift 2
            ;;
        --scope)
            [[ $# -ge 2 ]] || { echo "--scope requires control, publisher, sq-prepare, parallel-publish, publish-core, or publish-audit" >&2; exit 1; }
            scope="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: build.sh [--backend all|simt|simd] [--fast] [--scope control|publisher|sq-prepare|parallel-publish|publish-core|publish-audit] [--matrix-batch N] [--clean]"
            echo "SIMT diagnostics: build.sh --backend simt --fast (build/simt-fast/)"
            echo "--clean removes only the selected profile, not other builds."
            exit 0
            ;;
        *)
            echo "Usage: build.sh [--backend all|simt|simd] [--fast] [--scope control|publisher|sq-prepare|parallel-publish|publish-core|publish-audit] [--matrix-batch N] [--clean]" >&2
            exit 1
            ;;
    esac
done
[[ "$backend" == all || "$backend" == simt || "$backend" == simd ]] || {
    echo "--backend must be all, simt, or simd" >&2; exit 1;
}
[[ "$backend" != simd || "$fast_build" == OFF ]] || {
    echo "--fast only applies to SIMT; use --backend simd without --fast" >&2; exit 1;
}
if [[ "$matrix_batch" != 0 ]]; then
    [[ "$scope" == none && "$backend" == simt && "$fast_build" == OFF ]] || {
        echo "--matrix-batch requires --backend simt without --scope/--fast" >&2; exit 1;
    }
    scope=matrix
fi
[[ "$scope" == matrix || "$scope" == none || "$scope" == control || "$scope" == publisher || "$scope" == sq-prepare ||
   "$scope" == parallel-publish || "$scope" == publish-core || "$scope" == publish-audit ]] || {
    echo "--scope must be control, publisher, sq-prepare, parallel-publish, publish-core, or publish-audit" >&2; exit 1;
}
[[ "$scope" == none || ( "$backend" == simt && "$fast_build" == OFF ) ]] || {
    echo "--scope requires --backend simt without --fast" >&2; exit 1;
}
if [[ -z "${ASCEND_HOME_PATH:-}" ]]; then
    echo "ASCEND_HOME_PATH is not set. Run: source /usr/local/Ascend/cann/set_env.sh" >&2
    exit 1
fi
variant=full
[[ "$fast_build" == OFF ]] || variant=fast
profile="${backend}-${variant}"
[[ "$scope" == none ]] || profile="simt-scope-${scope}"
[[ "$scope" != matrix ]] || profile="simt-matrix-bs${matrix_batch}"
build_dir="${script_dir}/build/${profile}"
if [[ "$clean_build" == true ]]; then
    rm -rf -- "$build_dir"
fi
started_at=$SECONDS
echo "BUILD_CONFIG | Profile=${profile} | Backend=${backend} | Fast=${fast_build} | Directory=${build_dir}"
echo "BUILD_INPUT | AscendHome=${ASCEND_HOME_PATH} | Scope=${scope}"
if [[ "$scope" != none && -d "$build_dir" ]]; then
    rm -f -- "${build_dir}/scope-ready.sha256"
fi
cmake -S "$script_dir" -B "$build_dir" \
    -DCMAKE_ASC_ARCHITECTURES=dav-3510 \
    -DMULTI_JETTY_BACKEND="$backend" \
    -DMULTI_JETTY_DIAGNOSTIC_FAST_BUILD="$fast_build" \
    -DMULTI_JETTY_SCOPE_EXPERIMENT="$scope" \
    -DMULTI_JETTY_MATRIX_BATCH="$matrix_batch"
if [[ "$scope" == none ]]; then
    cmake --build "$build_dir" --verbose --parallel "${BUILD_JOBS:-$(nproc)}"
else
    # Readiness belongs to this exact binary, not merely a successful link or profile name.
    build_log="${build_dir}/scope-build.log"
    kernel_object="${build_dir}/CMakeFiles/multi_jetty_batch_perf.dir/simt_kernel.cpp.o"
    cmake --build "$build_dir" --verbose --parallel "${BUILD_JOBS:-2}" 2>&1 | tee "$build_log"
    mapfile -t resources < <(grep '^\[BISHENG\] SIMT VF Function properties for ' "$build_log" || true)
    if [[ ${#resources[@]} == 0 ]]; then
        # An unchanged device object may reuse its previously checked resource report.
        [[ -f "${build_dir}/scope-resource.txt" && -f "${build_dir}/scope-resource-object.sha256" ]] &&
            sha256sum --check --status "${build_dir}/scope-resource-object.sha256" || {
                echo "SCOPE_RESOURCE | READY=NO | Reason=missing-fresh-resource-report" >&2; exit 1;
            }
    else
        resource_pattern='Stack size: ([0-9]+) bytes, Used register number: ([0-9]+)'
        vf_batch=256
        vf_capacity=1024
        if [[ "$scope" == matrix ]]; then
            vf_batch="$matrix_batch"
            vf_capacity=$((matrix_batch * 8))
            ((vf_capacity >= 32)) || vf_capacity=32
            ((vf_capacity <= 1024)) || vf_capacity=1024
        fi
        vf_name="SimtParallelMultiJettyVfILj${vf_batch}ELj${vf_capacity}E"
        [[ ${#resources[@]} == 1 && "${resources[0]}" == *"${vf_name}"* &&
            "${resources[0]}" =~ $resource_pattern ]] || {
                echo "SCOPE_RESOURCE | READY=NO | Reason=unexpected-VF-report" >&2; exit 1;
            }
        stack_bytes="${BASH_REMATCH[1]}"
        registers="${BASH_REMATCH[2]}"
        [[ "$stack_bytes" -le 1152 && "$registers" -le 32 ]] || {
            echo "SCOPE_RESOURCE | READY=NO | StackBytes=${stack_bytes} | Registers=${registers}" >&2
            exit 1
        }
        printf 'SCOPE_RESOURCE | Profile=%s | VF=%s,%s | StackBytes=%s | Registers=%s | READY=YES\n' \
            "$profile" "$vf_batch" "$vf_capacity" "$stack_bytes" "$registers" > "${build_dir}/scope-resource.txt"
        sha256sum "$kernel_object" > "${build_dir}/scope-resource-object.sha256"
    fi
    cat "${build_dir}/scope-resource.txt"
    sha256sum "${build_dir}/multi_jetty_batch_perf" > "${build_dir}/scope-ready.sha256"
fi
echo "BUILD_COMPLETE | Profile=${profile} | Seconds=$((SECONDS - started_at))"
if [[ "$scope" == none ]]; then
    echo "Select this binary with: bash run.sh all --build-profile ${profile}"
else
    echo "Select ${profile} with the explicit scope parameters and control in README.md."
fi
