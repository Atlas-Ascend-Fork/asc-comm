#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

set -euo pipefail

mc2_install_root=
for option in "$@"; do
    case "$option" in
        --install-path=*) mc2_install_root="${option#*=}" ;;
        # CANN makeself prefixes the payload arguments with --<archive path>
        # and --<launch directory>. They are metadata, never install paths.
        --/*) ;;
        --quiet) ;;
        --help) echo 'Usage: install.sh --install-path=/absolute/opp/root'; exit 0 ;;
        *) echo "Unsupported installer option: ${option}" >&2; exit 2 ;;
    esac
done
if [[ -z "${mc2_install_root}" || "${mc2_install_root}" != /* ]]; then
    echo 'An explicit absolute --install-path=<OPP root> is required' >&2
    exit 2
fi
readonly mc2_install_payload="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/packages/vendors/asccomm_api_bench_ccu"
readonly mc2_install_parent="${mc2_install_root%/}/vendors"
readonly mc2_install_target="${mc2_install_parent}/asccomm_api_bench_ccu"
if [[ ! -f "${mc2_install_payload}/asccomm_api_bench_ccu_package.info" ||
      ! -f "${mc2_install_payload}/op_api/lib/libasccomm_api_bench_ccu_opapi.so" ]]; then
    echo 'Incomplete CCU operator payload' >&2
    exit 1
fi
python3 - "${mc2_install_payload}/asccomm_api_bench_ccu_package.info" <<'PY'
from pathlib import Path
import sys
fields = {}
for line in Path(sys.argv[1]).read_text().splitlines():
    key, separator, value = line.partition('=')
    if not separator or not key or key in fields:
        raise SystemExit('Invalid CCU marker')
    fields[key] = value
for key, value in {
    'package': 'asccomm_api_bench_ccu', 'schema': '3', 'mode': 'ccu',
    'operators': 'allgather,allreduce,reducescatter,alltoall,alltoallv,alltoallvwrite',
    'measurement_mode': 'full',
    'algorithm': 'runtime', 'runtime_algorithm_attr': '1', 'comm_engine_attr': '1',
    'supported_engines': '5,6', 'kernel_iterations': '1',
    'send_vector_layout': 'mission_major_2xranks_bytes', 'peer_alignment_bytes': '128',
    'report_layout': 'magic,wait_duration_ns',
    'window_owner': 'HcclGetHcclBuffer',
}.items():
    if fields.get(key) != value:
        raise SystemExit('Incompatible CCU marker field ' + key)
PY
mkdir -p -- "${mc2_install_parent}"
readonly mc2_install_lock="${mc2_install_parent}/.asccomm_api_bench_ccu.install.lock"
if ! mkdir -- "${mc2_install_lock}"; then
    echo "CCU installer lock exists: ${mc2_install_lock}" >&2
    exit 1
fi
mc2_install_stage=
trap 'if [[ -n "${mc2_install_stage}" ]]; then rm -rf -- "${mc2_install_stage}"; fi; rmdir -- "${mc2_install_lock}"' EXIT
if [[ -e "${mc2_install_target}" || -L "${mc2_install_target}" ]]; then
    echo "Existing vendor is preserved: ${mc2_install_target}; quiesce jobs and move it to a backup before upgrading" >&2
    exit 1
fi
mc2_install_stage="$(mktemp -d "${mc2_install_parent}/.asccomm_api_bench_ccu.stage.XXXXXX")"
cp -R -- "${mc2_install_payload}/." "${mc2_install_stage}/"
mkdir -p -- "${mc2_install_stage}/bin"
{
    printf '#!/usr/bin/env bash\nexport ASCEND_CUSTOM_OPP_PATH=%q${ASCEND_CUSTOM_OPP_PATH:+:$ASCEND_CUSTOM_OPP_PATH}\n' "${mc2_install_target}"
    printf 'export LD_LIBRARY_PATH=%q${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\n' "${mc2_install_target}/op_api/lib"
} > "${mc2_install_stage}/bin/set_env.bash"
# The atomic move is the only publication step. No existing vendor is removed.
mv -- "${mc2_install_stage}" "${mc2_install_target}"
mc2_install_stage=
echo "Installed ${mc2_install_target}; source its bin/set_env.bash before backend use"
