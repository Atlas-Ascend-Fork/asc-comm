#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

"""Offline or post-launch device profiling analysis for one asccomm_api_bench run."""

import argparse
import json
from pathlib import Path
import sys

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))
from profiling.pipeline import analyze_run  # noqa: E402


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path, required=True)
    parser.add_argument(
        "--records",
        type=Path,
        required=True,
        help="One job's rank-zero RecordWriter JSONL",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--msprof", help="CANN msprof executable for raw profiling export"
    )
    args = parser.parse_args(argv)
    result = analyze_run(
        args.run_dir, args.records, args.output_dir, msprof=args.msprof
    )
    print(json.dumps(result, ensure_ascii=False))
    return result["returncode"]


if __name__ == "__main__":
    sys.exit(main())
