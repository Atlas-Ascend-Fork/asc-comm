#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""提取CCU direct样例列表：样例名(case)、路径(path)、场景(scenario)。

读取examples/ccu/ccu_direct下每个样例目录的README.md：
- 无`SCENARIO_NUM`表格行：单场景样例，scenario为"-"，样例名为ccu_direct-<目录名>；
- 有`SCENARIO_NUM`表格行：多场景样例，取值单元格中`N`/`M`表示场景N和M、`N`~`M`表示场景N到M，
  每个场景单独一行，样例名为ccu_direct-<目录名>-SC<N>。
结果输出为JSON数组（每行一个dict），供run_presmoke.sh使用。
"""

import argparse
import json
import re
from pathlib import Path

SCENARIO_CELL = "`SCENARIO_NUM`"
RANGE_SEPARATORS = ("~", "～")


def parse_scenario_cell(readme_path):
    """从README.md中解析`SCENARIO_NUM`表格行的取值单元格。

    返回场景号列表（int）；没有`SCENARIO_NUM`行时返回None，表示单场景样例。
    """
    if not readme_path.is_file():
        return None
    lines = readme_path.read_text(encoding="utf-8").splitlines()
    for line in lines:
        cells = [cell.strip() for cell in line.split("|")]
        for idx, cell in enumerate(cells):
            if cell != SCENARIO_CELL or idx + 1 >= len(cells):
                continue
            return parse_value_cell(cells[idx + 1])
    return None


def parse_value_cell(value_cell):
    """解析取值单元格，如`1`/`2`、`1`~`7`，返回场景号列表。"""
    numbers = [int(item) for item in re.findall(r"`(\d+)`", value_cell)]
    if not numbers:
        return None
    if any(sep in value_cell for sep in RANGE_SEPARATORS):
        return list(range(numbers[0], numbers[-1] + 1))
    return sorted(set(numbers))


def collect_cases(examples_dir, repo_root):
    """扫描样例目录，构建case/path/scenario记录列表。"""
    cases = []
    for sample_dir in sorted(examples_dir.iterdir()):
        if not sample_dir.is_dir():
            continue
        rel_path = sample_dir.relative_to(repo_root).as_posix()
        scenarios = parse_scenario_cell(sample_dir / "README.md")
        if not scenarios:
            cases.append(
                {
                    "case": f"ccu_direct-{sample_dir.name}",
                    "path": rel_path,
                    "scenario": "-",
                }
            )
            continue
        for scenario in scenarios:
            cases.append(
                {
                    "case": f"ccu_direct-{sample_dir.name}-SC{scenario}",
                    "path": rel_path,
                    "scenario": str(scenario),
                }
            )
    return cases


def parse_args():
    parser = argparse.ArgumentParser(
        description="Extract CCU direct sample cases into presmoke case_list.json."
    )
    parser.add_argument(
        "--examples-dir",
        type=Path,
        default=None,
        help="ccu_direct样例根目录，默认为仓库内examples/ccu/ccu_direct",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="输出JSON路径，默认为仓库内presmoke_reports/case_list.json",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    examples_dir = args.examples_dir or repo_root / "examples" / "ccu" / "ccu_direct"
    output = args.output or repo_root / "presmoke_reports" / "case_list.json"
    if not examples_dir.is_dir():
        raise SystemExit(f"[parse_cases][ERROR] examples dir not found: {examples_dir}")

    cases = collect_cases(examples_dir, repo_root)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as file:
        json.dump(cases, file, ensure_ascii=False, indent=2)
        file.write("\n")
    print(f"[parse_cases] {len(cases)} cases written to {output}")


if __name__ == "__main__":
    main()
