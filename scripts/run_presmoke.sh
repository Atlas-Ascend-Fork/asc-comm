#!/usr/bin/env bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
# CCU样例前冒烟测试入口：样例提取 -> 编译/执行流水线 -> 结果汇总。
# CPU并行编译与硬件串行执行重叠：编译完成的样例立即进入就绪队列排队执行，
# 无需等待全部样例编译完成。
# 用法：bash run_presmoke.sh [选项] [pattern ...]，不带pattern时执行 ccu_direct 与 ccu_builtin 全部样例。pattern匹配规则：
#   完整样例名    ccu_direct-04_allreduce-SC1   仅该场景
#   样例名        ccu_direct-04_allreduce       该样例全部场景
#   目录名        04_allreduce                  该样例全部场景
#   通配符        '*allreduce*'                 按glob匹配样例名
# 结果输出到asc-comm/presmoke_reports/：
#   case_list.json   样例列表（样例名/路径/场景）
#   log/             每个样例一个子目录，归档该样例全部材料：console.txt（执行打屏与退出码，
#                    匹配"test pass"判定通过，失败时打屏尾部）、build.txt与build.status（编译日志
#                    与状态）、plog/（该样例的plog与device日志）、data/（有scripts目录样例的输入
#                    与golden数据）
#   result.txt       汇总结果
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

EXAMPLES_DIRS=(
    "${REPO_ROOT}/examples/ccu/ccu_direct"
    "${REPO_ROOT}/examples/cc/ccu_builtin"
)
REPORT_DIR="${REPO_ROOT}/presmoke_reports"
LOG_DIR="${REPORT_DIR}/log"
CASE_LIST="${REPORT_DIR}/case_list.json"
RESULT_FILE="${REPORT_DIR}/result.txt"

EXAMPLES_ARGS=()
for examples_dir in "${EXAMPLES_DIRS[@]}"; do
    EXAMPLES_ARGS+=(--examples-dir "${examples_dir}")
done

usage() {
    cat <<'EOF'
用法: bash run_presmoke.sh [选项] [pattern ...]

对CCU样例执行前冒烟：样例提取 -> 编译/执行流水线 -> 结果汇总。
CPU并行编译与硬件串行执行重叠，编译完成的样例直接排队执行。
不带pattern时执行 ccu_direct 与 ccu_builtin 全部样例（分别位于 examples/ccu/ccu_direct 与 examples/cc/ccu_builtin）。每个pattern按以下规则匹配用例（可叠加多个pattern）：
    完整样例名    ccu_direct-04_allreduce-SC1   仅该场景
    样例名        ccu_direct-04_allreduce       该样例全部场景
    目录名        04_allreduce                  该样例全部场景
    通配符        '*allreduce*'                 按glob匹配样例名

选项:
    -l, --list          列出全部可用样例并退出
    -h, --help          显示本帮助并退出

环境变量:
    DEVICES             mem2mem类样例使用的设备列表，默认0,1
    COUNT               单rank元素个数，默认256
    PARALLEL_BUILDS     并行编译路数，默认4
    EXEC_TIMEOUT        单样例执行超时（秒），默认600
    ERROR_LOG_LINES     样例失败时打屏的日志尾部行数，默认50
EOF
}

SELECTED_PATTERNS=()
LIST_ONLY=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        -l | --list)
            LIST_ONLY=1
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        -*)
            echo "[presmoke][ERROR] unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
        *)
            if [[ -n "$1" ]]; then
                SELECTED_PATTERNS+=("$1")
            fi
            ;;
    esac
    shift
done

# 仅列出可用样例：只依赖parse_cases.py，无需CANN环境
if [[ "${LIST_ONLY}" == "1" ]]; then
    TMP_CASE_LIST="$(mktemp)"
    trap 'rm -f "${TMP_CASE_LIST}"' EXIT
    python3 "${SCRIPT_DIR}/parse_cases.py" "${EXAMPLES_ARGS[@]}" --output "${TMP_CASE_LIST}"
    python3 - "${TMP_CASE_LIST}" <<'PYEOF'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as file:
    for row in json.load(file):
        print(row["case"])
PYEOF
    exit 0
fi

# 环境准备：ASCEND_HOME_PATH必须已配置，并开启CCU调度模式
if [[ -z "${ASCEND_HOME_PATH:-}" ]]; then
    echo "[presmoke][ERROR] ASCEND_HOME_PATH is not set; source CANN set_env.sh first." >&2
    exit 1
fi
export HCCL_OP_EXPANSION_MODE=CCU_SCHED

# Presmoke 不加载 asccomm_aicpu.tar.gz。
package_load_ini="${ASCEND_HOME_PATH}/conf/ascend_package_load.ini"
if grep -q '^name:asccomm_aicpu.tar.gz$' "${package_load_ini}"; then
    cp "${package_load_ini}" "${package_load_ini}.asc-comm-presmoke.bak"
    sed -i '/name:asccomm_aicpu.tar.gz/,+4d' "${package_load_ini}"
    echo "[presmoke] Disabled asccomm_aicpu.tar.gz loading in ${package_load_ini}"
fi

# mem2mem类样例（有scripts目录）的执行参数：rank数必须与DEVICES数量一致
DEVICES="${DEVICES:-0,1}"
COUNT="${COUNT:-256}"
RANK_SIZE="$(awk -F',' '{print NF}' <<<"${DEVICES}")"
# 并行编译的样例数、单样例执行超时（秒）、失败时打屏日志尾部行数
PARALLEL_BUILDS="${PARALLEL_BUILDS:-4}"
EXEC_TIMEOUT="${EXEC_TIMEOUT:-600}"
ERROR_LOG_LINES="${ERROR_LOG_LINES:-50}"

# 样例提取
rm -rf "${LOG_DIR}"
rm -f "${CASE_LIST}" "${RESULT_FILE}"
mkdir -p "${LOG_DIR}"
python3 "${SCRIPT_DIR}/parse_cases.py" "${EXAMPLES_ARGS[@]}" --output "${CASE_LIST}"

CASE_TABLE="$(python3 - "${CASE_LIST}" <<'PYEOF'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as file:
    cases = json.load(file)
for row in cases:
    print("|".join((row["case"], row["path"], row["scenario"])))
PYEOF
)"
if [[ -z "${CASE_TABLE}" ]]; then
    echo "[presmoke][ERROR] no case found in ${CASE_LIST}" >&2
    exit 1
fi
ALL_TABLE="${CASE_TABLE}"

# 用例筛选：pattern支持完整样例名、样例名（含全部场景）、目录名（含全部场景）和通配符
# 注意：${pattern}在[[ ]]中不加引号，保证通配符按glob生效
if [[ ${#SELECTED_PATTERNS[@]} -gt 0 ]]; then
    CASE_TABLE="$(while IFS='|' read -r case_name path scenario; do
        for pattern in "${SELECTED_PATTERNS[@]}"; do
            if [[ "${case_name}" == ${pattern} \
                || "${case_name#*-}" == ${pattern} \
                || "${case_name#*-}" == ${pattern}-* ]]; then
                echo "${case_name}|${path}|${scenario}"
                break
            fi
        done
    done <<<"${CASE_TABLE}")"
    if [[ -z "${CASE_TABLE}" ]]; then
        echo "[presmoke][ERROR] no case matched: ${SELECTED_PATTERNS[*]}" >&2
        echo "[presmoke][ERROR] available cases:" >&2
        while IFS='|' read -r case_name path scenario; do
            echo "  ${case_name}" >&2
        done <<<"${ALL_TABLE}"
        exit 1
    fi
fi

# 临时屏蔽清单：冒烟基座镜像周更（v1.1003→v1.1013）后，新基座选择器在A5拓扑上对这两个样例
# 报"CCU_SCHED not match, fail without AICPU fallback"（选择层拒选，与asc-comm PR无关）。
# 待基座选择器/算法迁移修复后从清单移除；可用SKIP_CASES环境变量（空格分隔）覆盖。
if [[ -z "${SKIP_CASES:-}" ]]; then
    SKIP_CASES=(
        "ccu_builtin-01_allgather"
        "ccu_builtin-02_allgather_add"
    )
else
    read -ra SKIP_CASES <<<"${SKIP_CASES}"
fi

# 屏蔽在pattern筛选之后生效：命中的用例从待执行列表剔除并打印提示
CASE_TABLE="$(while IFS='|' read -r case_name path scenario; do
    skipped=false
    for skip_pattern in "${SKIP_CASES[@]}"; do
        if [[ "${case_name}" == ${skip_pattern} ]]; then
            echo "[presmoke] skip ${case_name} (in SKIP_CASES)" >&2
            skipped=true
            break
        fi
    done
    [[ "${skipped}" == "true" ]] || echo "${case_name}|${path}|${scenario}"
done <<<"${CASE_TABLE}")"
if [[ -z "${CASE_TABLE}" ]]; then
    echo "[presmoke][ERROR] no case left after SKIP_CASES filter" >&2
    exit 1
fi

CASE_TOTAL="$(wc -l <<<"${CASE_TABLE}")"
ALL_TOTAL="$(wc -l <<<"${ALL_TABLE}")"
echo "[presmoke] ${CASE_TOTAL} cases loaded from ${CASE_LIST}"
if [[ ${#SELECTED_PATTERNS[@]} -gt 0 ]]; then
    echo "[presmoke] selected ${CASE_TOTAL} of ${ALL_TOTAL} cases by: ${SELECTED_PATTERNS[*]}"
    while IFS='|' read -r case_name path scenario; do
        echo "[presmoke]   ${case_name}"
    done <<<"${CASE_TABLE}"
fi

# 流水线：CPU并行编译（BUILD_ONLY）与硬件串行执行（EXEC_ONLY）重叠，
# 编译完成的样例立即写入就绪队列，执行器串行消费，无需等全部编译完成
PIPE_FILE="${REPORT_DIR}/presmoke_queue.fifo"
rm -f "${PIPE_FILE}"
mkfifo "${PIPE_FILE}"
trap 'rm -f "${PIPE_FILE}"' EXIT

# 编译worker：编译完成后写状态文件，并把用例写入就绪队列（编译失败也入队，由执行器按状态跳过）
build_one() {
    local case_name="$1"
    local path="$2"
    local scenario="$3"
    local case_dir="${LOG_DIR}/${case_name}"
    local log_file="${case_dir}/build.txt"
    local status_file="${case_dir}/build.status"
    local rc=0
    mkdir -p "${case_dir}"
    if [[ "${scenario}" == "-" ]]; then
        BUILD_ONLY=1 bash "${REPO_ROOT}/${path}/run.sh" >"${log_file}" 2>&1 || rc=$?
    else
        SCENARIO_NUM="${scenario}" BUILD_ONLY=1 bash "${REPO_ROOT}/${path}/run.sh" >"${log_file}" 2>&1 || rc=$?
    fi
    echo "${rc}" >"${status_file}"
    echo "${case_name}|${path}|${scenario}" >"${PIPE_FILE}"
    return 0
}
export -f build_one
export REPO_ROOT LOG_DIR PIPE_FILE

# 样例失败时打屏日志尾部，便于在CI控制台快速定位报错原因
print_failed_log_tail() {
    local label="$1"
    local log_file="$2"
    if [[ ! -s "${log_file}" ]]; then
        echo "[presmoke] ${label} failed, log is empty or missing: ${log_file}"
        return
    fi
    echo "[presmoke] ---------------- ${label} failed, error log (last ${ERROR_LOG_LINES} lines of ${log_file}) ----------------"
    tail -n "${ERROR_LOG_LINES}" "${log_file}"
    echo "[presmoke] ---------------- end of ${label} error log ----------------"
}

run_case() {
    local case_name="$1"
    local path="$2"
    local scenario="$3"
    local sample_dir="${REPO_ROOT}/${path}"
    local case_dir="${LOG_DIR}/${case_name}"
    local log_file="${case_dir}/console.txt"
    local rc_file="${case_dir}/exit_code"
    local status_file="${case_dir}/build.status"
    local rc=0

    mkdir -p "${case_dir}"
    : >"${log_file}"
    echo "[presmoke] run ${case_name}"

    if [[ ! -f "${status_file}" || "$(cat "${status_file}")" != "0" ]]; then
        echo "[presmoke] build failed or missing, skip exec; see log/${case_name}/build.txt" | tee -a "${log_file}"
        echo "1" >"${rc_file}"
        print_failed_log_tail "${case_name} build" "${case_dir}/build.txt"
        return 1
    fi

    # 有scripts目录的样例：输入和golden数据放在该样例目录的data下，不写入样例源码目录
    local input_dir="${case_dir}/data/input"
    local output_dir="${case_dir}/data/output"
    # 该样例的plog/device日志路由到自己的目录，slog归档时可与打屏一一对应
    local plog_dir="${case_dir}/plog"
    mkdir -p "${plog_dir}"

    if [[ -f "${sample_dir}/scripts/gen_data.py" ]]; then
        (cd "${sample_dir}" && python3 scripts/gen_data.py --rank-size "${RANK_SIZE}" --count "${COUNT}" \
            --input-dir "${input_dir}" --output-dir "${output_dir}") \
            >>"${log_file}" 2>&1 || rc=1
    fi

    if [[ "${rc}" == "0" ]]; then
        local env_args=(EXEC_ONLY=1 DEVICES="${DEVICES}" COUNT="${COUNT}" INPUT_DIR="${input_dir}" OUTPUT_DIR="${output_dir}" ASCEND_PROCESS_LOG_PATH="${plog_dir}")
        if [[ "${scenario}" != "-" ]]; then
            env_args+=("SCENARIO_NUM=${scenario}")
        fi
        (cd "${sample_dir}" && timeout "${EXEC_TIMEOUT}" env "${env_args[@]}" bash run.sh) \
            >>"${log_file}" 2>&1 || rc=1
    fi

    # 有scripts目录的样例执行后校验输出，校验通过在日志中打印"test pass!"
    if [[ "${rc}" == "0" && -f "${sample_dir}/scripts/verify_result.py" ]]; then
        (cd "${sample_dir}" && python3 scripts/verify_result.py --rank-size "${RANK_SIZE}" --count "${COUNT}" \
            --output-dir "${output_dir}" --golden "${output_dir}/golden.bin") \
            >>"${log_file}" 2>&1 || rc=1
    fi

    echo "${rc}" >"${rc_file}"
    if [[ "${rc}" == "0" ]]; then
        echo "[presmoke] ${case_name} finished" | tee -a "${log_file}"
    else
        echo "[presmoke] ${case_name} finished with error" | tee -a "${log_file}"
        print_failed_log_tail "${case_name}" "${log_file}"
    fi
    return "${rc}"
}

# 执行器：串行消费就绪队列，读到EOF（所有编译入队完毕且队列排空）后退出
run_executor() {
    local row case_name path scenario
    while IFS= read -r row; do
        IFS='|' read -r case_name path scenario <<<"${row}"
        run_case "${case_name}" "${path}" "${scenario}" || true
    done <"${PIPE_FILE}"
}

PIPELINE_START="$(date +%s)"
echo "[presmoke] building with up to ${PARALLEL_BUILDS} parallel jobs; finished builds queue for serial exec"

# 执行器先于队列写端打开前启动，避免继承写端导致收不到EOF
run_executor &
EXECUTOR_PID=$!

# 主进程持有队列读写端：写端保证执行器在两次入队之间不会读到EOF
exec 3<>"${PIPE_FILE}"

if ! printf '%s\n' "${CASE_TABLE}" | xargs -d '\n' -P "${PARALLEL_BUILDS}" -I '{}' \
    bash -c 'IFS="|" read -r case_name path scenario <<<"{}"; build_one "${case_name}" "${path}" "${scenario}"'; then
    echo "[presmoke][ERROR] build launcher reported failure; continuing with queued cases" >&2
fi
echo "[presmoke] all builds finished in $(( $(date +%s) - PIPELINE_START ))s"

# 关闭主进程写端后，执行器消费完队列剩余用例即读到EOF退出
exec 3>&-
wait "${EXECUTOR_PID}" || true
echo "[presmoke] build+exec pipeline finished in $(( $(date +%s) - PIPELINE_START ))s"

# 结果汇总：执行退出码为0且日志中匹配"test pass"才判定样例通过
summarize() {
    local total=0
    local pass_count=0
    local fail_count=0
    {
        echo "==================== presmoke result ===================="
        while IFS='|' read -r case_name path scenario; do
            total=$((total + 1))
            local case_rc="1"
            if [[ -f "${LOG_DIR}/${case_name}/exit_code" ]]; then
                case_rc="$(cat "${LOG_DIR}/${case_name}/exit_code")"
            fi
            if [[ "${case_rc}" == "0" ]] && grep -q "test pass" "${LOG_DIR}/${case_name}/console.txt" 2>/dev/null; then
                pass_count=$((pass_count + 1))
                printf '%-50s %s\n' "${case_name}" "PASS"
            else
                fail_count=$((fail_count + 1))
                printf '%-50s %s\n' "${case_name}" "FAIL"
            fi
        done <<<"${CASE_TABLE}"
        echo "--------------------------------------------------------"
        echo "total: ${total}, pass: ${pass_count}, fail: ${fail_count}"
        echo "logs: per case under ${LOG_DIR}/<case_name>/"
    } >"${RESULT_FILE}"
    cat "${RESULT_FILE}"
    [[ "${fail_count}" == "0" ]]
}

summarize
