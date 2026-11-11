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
# 由PR-pipeline_asc-comm.yml的PreSmoke阶段经obs下载后执行，依赖WORKSPACE/obs_path/repo_name/pr_id环境变量。
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
smoke_package="cann-asc-comm_linux-x86_64.run"
wget -nv -O "${smoke_package}" \
  "https://ascend-ci.obs.cn-north-4.myhuaweicloud.com/${obs_path}/${smoke_package}" 2>/dev/null

if [ ! -f "${smoke_package}" ] || [ ! -s "${smoke_package}" ]; then
  echo "No custom package found, This PR no need execute smoke."
  rm -f "${smoke_package}"
  exit 0
fi

chmod +x ${smoke_package}
yes "y" | bash "${smoke_package}" --full --install-path=/usr/local/Ascend --quiet
source /usr/local/Ascend/cann/set_env.sh

(cd ./scripts && bash run_presmoke.sh 2>&1 | tee -a "${WORKSPACE}/run_test.log")
if [ -d "presmoke_reports" ]; then
  mv presmoke_reports /root/ascend/log
fi

# 打包plog与冒烟报告
tar -zcf "${WORKSPACE}/slog.tar.gz" -C /root/ascend log

# ==============================
# 检查 NPU 状态
# ==============================
log "checking NPU status ..."
mkdir -p "${WORKSPACE}/npu_log"
npu-smi info 2>&1 | tee "${WORKSPACE}/npu_log/npu_info.log"
if grep "dcmi module initialize failed" "${WORKSPACE}/npu_log/npu_info.log"; then
  date_time=$(date '+%Y%m%d.%H%M%S')
  echo "$date_time : ${repo_name}_${pr_id} dcmi module initialize failed" >> "${WORKSPACE}/npu_log/`date +%Y%m%d`.log"
  python3 /home/aa/check_npu.py $repo_name $pr_id `cat /home/aa/hostname`
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
