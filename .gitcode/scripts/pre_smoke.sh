#!/bin/bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
# CCU direct样例前冒烟CI入口：安装PR编译产物 -> 运行scripts/run_presmoke.sh -> 打包日志 -> 检查结果。
# 由PR-pipeline_asc-comm.yml的PreSmoke阶段经obs下载后执行，
# 依赖WORKSPACE/run_url（编译产物上传地址）/merge_commit（PR合入提交）/repo_name/pr_id环境变量。
set +e

shopt -s extglob


log() {
  local dt
  dt=$(date '+%Y%m%d.%H%M%S')
  echo "===================================================================="
  echo "$dt : $*"
  echo "===================================================================="
}

log "init test case, please wait ..."
rm -rf /root/ascend/log
mkdir -p /root/ascend/log

# ==============================
# 运行 asc-comm CCU direct 样例冒烟主流程
# ==============================
export ASCEND_GLOBAL_LOG_LEVEL=2
export ASCEND_SLOG_PRINT_TO_STDOUT=0

log "start run test case, please wait ..."

source /usr/local/Ascend/cann/set_env.sh

cd ${WORKSPACE}

# run_url由流水线传入（编译阶段obs-upload的真实落地地址）。
# 为空说明管道断裂（编译未产出或未上传包），必须判失败而不是静默跳过冒烟。
if [ -z "${run_url:-}" ]; then
  echo "ERROR: run_url is empty, presmoke cannot pick the asc-comm package" >&2
  exit 1
fi
smoke_package=$(basename "${run_url}")
echo "smoke package for this job: ${smoke_package}"

# 下载失败或空文件一律判失败：编译恒产包，下载不到必为事故（网络/对象损坏等）
wget -nv -O "${smoke_package}" "${run_url}"
if [ ! -f "${smoke_package}" ] || [ ! -s "${smoke_package}" ]; then
  echo "ERROR: failed to download smoke package from ${run_url}" >&2
  rm -f "${smoke_package}"
  exit 1
fi

chmod +x "${smoke_package}"
yes "y" | bash "${smoke_package}" --full --install-path=/usr/local/Ascend --quiet
install_rc=$?
if [ ${install_rc} -ne 0 ]; then
  echo "ERROR: install smoke package failed, rc=${install_rc}" >&2
  exit 1
fi
source /usr/local/Ascend/cann/set_env.sh

# 校验已装包与PR同源：装到旧包测出的"test pass"是误导性的假绿灯
if [ -n "${merge_commit:-}" ]; then
  installed_version_info=""
  for cann_root in /usr/local/Ascend/cann /usr/local/Ascend/ascend-toolkit/latest /usr/local/Ascend; do
    if [ -f "${cann_root}/var/asc-comm-dev-patch/version.info" ]; then
      installed_version_info="${cann_root}/var/asc-comm-dev-patch/version.info"
      break
    fi
  done
  if [ -z "${installed_version_info}" ]; then
    echo "ERROR: installed package version.info not found, cannot verify package identity" >&2
    exit 1
  fi
  installed_commit=$(awk -F= '$1 == "GitCommit" {sub(/^[^=]*=/, ""); print; exit}' "${installed_version_info}")
  if [ -z "${installed_commit}" ]; then
    echo "ERROR: installed package version.info has no GitCommit, cannot verify package identity" >&2
    exit 1
  fi
  if [ "${installed_commit}" != "${merge_commit}" ]; then
    echo "ERROR: installed package commit ${installed_commit} != PR merge commit ${merge_commit}" >&2
    exit 1
  fi
  echo "installed package commit verified: ${installed_commit}"
else
  echo "WARNING: merge_commit is empty, skip installed-package commit verification" >&2
fi

(cd ./scripts && bash run_presmoke.sh 2>&1 | tee -a "${WORKSPACE}/run_test.log")
if [ -d "presmoke_reports" ]; then
  mv presmoke_reports /root/ascend/log
fi

# ==============================
# 检查 NPU 状态（失败不立即退出，先完成归档再判定）
# ==============================
log "checking NPU status ..."
mkdir -p "${WORKSPACE}/npu_log"
npu-smi info 2>&1 | tee "${WORKSPACE}/npu_log/npu_info.log"
if grep "dcmi module initialize failed" "${WORKSPACE}/npu_log/npu_info.log"; then
  date_time=$(date '+%Y%m%d.%H%M%S')
  echo "$date_time : ${repo_name}_${pr_id} dcmi module initialize failed" >> "${WORKSPACE}/npu_log/`date +%Y%m%d`.log"
  python3 /home/aa/check_npu.py $repo_name $pr_id `cat /home/aa/hostname`
  npu_check_failed=1
else
  npu_check_failed=0
fi

# ==============================
# 打包plog与冒烟报告（presmoke_reports + 样例plog + run_test.log + npu_log）
# ==============================
cp -f "${WORKSPACE}/run_test.log" /root/ascend/log/ 2>/dev/null
cp -r "${WORKSPACE}/npu_log" /root/ascend/log/ 2>/dev/null
tar -zcf "${WORKSPACE}/slog.tar.gz" -C /root/ascend log

if [ "${npu_check_failed}" = "1" ]; then
  exit 1
fi

# ==============================
# 检查测试结果
# ==============================
log "checking test results ..."

date_time=`date +%Y%m%d`"."`date +%H%M%S`
if grep -q -E "total: [1-9][0-9]*, pass: [0-9]+, fail: 0" "${WORKSPACE}/run_test.log"; then
  echo "$date_time : run test case success"
else
  echo "$date_time : run test case failed"
  exit 1
fi
