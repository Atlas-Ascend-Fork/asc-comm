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
# CCU direct样例前冒烟测试入口：样例提取 -> 编译/执行流水线 -> 结果汇总。
# CPU并行编译与硬件串行执行重叠：编译完成的样例立即进入就绪队列排队执行，
# 无需等待全部样例编译完成。
# 用法：bash run_presmoke.sh [选项] [pattern ...]，不带pattern时执行全部样例。pattern匹配规则：
#   完整样例名    ccu_direct-04_allreduce-SC1   仅该场景
#   样例名        ccu_direct-04_allreduce       该样例全部场景
#   目录名        04_allreduce                  该样例全部场景
#   通配符        '*allreduce*'                 按glob匹配样例名
# 结果输出到asc-comm/presmoke_reports/：
#   case_list.json   样例列表（样例名/路径/场景）
#   build_log/       每个样例的编译日志和编译状态
#   log/             每个样例的执行日志（仅写文件不打屏，匹配"test pass"判定通过）
#   data/            有scripts目录样例的输入和golden数据（避免写入样例目录）
#   result.txt       汇总结果
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

EXAMPLES_DIR="${REPO_ROOT}/examples/ccu/ccu_direct"
REPORT_DIR="${REPO_ROOT}/presmoke_reports"
LOG_DIR="${REPORT_DIR}/log"
BUILD_LOG_DIR="${REPORT_DIR}/build_log"
DATA_DIR="${REPORT_DIR}/data"
CASE_LIST="${REPORT_DIR}/case_list.json"
RESULT_FILE="${REPORT_DIR}/result.txt"

usage() {
    cat <<'EOF'
用法: bash run_presmoke.sh [选项] [pattern ...]

对CCU direct样例执行前冒烟：样例提取 -> 编译/执行流水线 -> 结果汇总。
CPU并行编译与硬件串行执行重叠，编译完成的样例直接排队执行。
不带pattern时执行全部样例。每个pattern按以下规则匹配用例（可叠加多个pattern）：
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
    python3 "${SCRIPT_DIR}/parse_cases.py" --examples-dir "${EXAMPLES_DIR}" --output "${TMP_CASE_LIST}"
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

# mem2mem类样例（有scripts目录）的执行参数：rank数必须与DEVICES数量一致
DEVICES="${DEVICES:-0,1}"
COUNT="${COUNT:-256}"
RANK_SIZE="$(awk -F',' '{print NF}' <<<"${DEVICES}")"
# 并行编译的样例数、单样例执行超时（秒）
PARALLEL_BUILDS="${PARALLEL_BUILDS:-4}"
EXEC_TIMEOUT="${EXEC_TIMEOUT:-600}"

# 样例提取
rm -rf "${LOG_DIR}" "${BUILD_LOG_DIR}" "${DATA_DIR}"
rm -f "${CASE_LIST}" "${RESULT_FILE}"
mkdir -p "${LOG_DIR}" "${BUILD_LOG_DIR}" "${DATA_DIR}"
python3 "${SCRIPT_DIR}/parse_cases.py" --examples-dir "${EXAMPLES_DIR}" --output "${CASE_LIST}"

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
                || "${case_name}" == "ccu_direct-"${pattern} \
                || "${case_name}" == ${pattern}-* \
                || "${case_name}" == "ccu_direct-"${pattern}"-"* ]]; then
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
    local log_file="${BUILD_LOG_DIR}/build_${case_name}.txt"
    local status_file="${BUILD_LOG_DIR}/build_${case_name}.status"
    local rc=0
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
export REPO_ROOT BUILD_LOG_DIR PIPE_FILE

run_case() {
    local case_name="$1"
    local path="$2"
    local scenario="$3"
    local sample_dir="${REPO_ROOT}/${path}"
    local log_file="${LOG_DIR}/${case_name}.txt"
    local status_file="${BUILD_LOG_DIR}/build_${case_name}.status"
    local rc=0

    : >"${log_file}"
    echo "[presmoke] run ${case_name}"

    if [[ ! -f "${status_file}" || "$(cat "${status_file}")" != "0" ]]; then
        echo "[presmoke] build failed or missing, skip exec; see build_log/build_${case_name}.txt" | tee -a "${log_file}"
        return 1
    fi

    # 有scripts目录的样例：输入和golden数据统一放在presmoke_reports/data下，不写入样例目录
    local input_dir="${DATA_DIR}/${case_name}/input"
    local output_dir="${DATA_DIR}/${case_name}/output"

    if [[ -f "${sample_dir}/scripts/gen_data.py" ]]; then
        (cd "${sample_dir}" && python3 scripts/gen_data.py --rank-size "${RANK_SIZE}" --count "${COUNT}" \
            --input-dir "${input_dir}" --output-dir "${output_dir}") \
            >>"${log_file}" 2>&1 || rc=1
    fi

    if [[ "${rc}" == "0" ]]; then
        local env_args=(EXEC_ONLY=1 DEVICES="${DEVICES}" COUNT="${COUNT}" INPUT_DIR="${input_dir}" OUTPUT_DIR="${output_dir}")
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

    if [[ "${rc}" == "0" ]]; then
        echo "[presmoke] ${case_name} finished" | tee -a "${log_file}"
    else
        echo "[presmoke] ${case_name} finished with error" | tee -a "${log_file}"
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

# 结果汇总：执行日志中匹配"test pass"判定样例通过
summarize() {
    local total=0
    local pass_count=0
    local fail_count=0
    {
        echo "==================== presmoke result ===================="
        while IFS='|' read -r case_name path scenario; do
            total=$((total + 1))
            if grep -q "test pass" "${LOG_DIR}/${case_name}.txt" 2>/dev/null; then
                pass_count=$((pass_count + 1))
                printf '%-50s %s\n' "${case_name}" "PASS"
            else
                fail_count=$((fail_count + 1))
                printf '%-50s %s\n' "${case_name}" "FAIL"
            fi
        done <<<"${CASE_TABLE}"
        echo "--------------------------------------------------------"
        echo "total: ${total}, pass: ${pass_count}, fail: ${fail_count}"
        echo "logs: ${LOG_DIR}, build logs: ${BUILD_LOG_DIR}"
    } >"${RESULT_FILE}"
    cat "${RESULT_FILE}"
    [[ "${fail_count}" == "0" ]]
}

summarize
