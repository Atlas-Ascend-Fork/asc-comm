#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software: you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License is
# distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and limitations under the License.
# ----------------------------------------------------------------------------------------------------------
"""Convert PERF_DATA records to a CSV or Excel workbook."""

import argparse
import csv
import re
import sys
from pathlib import Path
from typing import Dict, List

from openpyxl import Workbook


RECORD_PATTERN = re.compile(r"^PERF_DATA\s*\|")
FIELD_PATTERN = re.compile(r"\s*([^=|]+)\s*=\s*([^|]*?)\s*(?=\||$)")


def parse_line(line: str) -> Dict[str, str]:
    """Parse one PERF_DATA line and return its left/right field values."""
    if not RECORD_PATTERN.match(line):
        return {}

    fields = {}
    for match in FIELD_PATTERN.finditer(line):
        key = match.group(1).strip()
        value = match.group(2).strip()
        if key:
            fields[key] = value
    return fields


def parse_streams(streams) -> List[Dict[str, str]]:
    records = []
    for stream in streams:
        with stream:
            for line in stream:
                record = parse_line(line.rstrip("\n"))
                if record:
                    records.append(record)
    return records


def write_csv(records: List[Dict[str, str]], output: Path) -> None:
    headers = list(records[0].keys())
    with output.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=headers, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(records)


def write_excel(records: List[Dict[str, str]], output: Path) -> None:
    workbook = Workbook()
    sheet = workbook.active
    sheet.title = "PERF_DATA"

    headers = list(records[0].keys())
    sheet.append(headers)
    for record in records:
        sheet.append([record.get(header, "") for header in headers])

    workbook.save(output)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Parse PERF_DATA lines and write perf_data.xlsx (default) or perf_data.csv."
    )
    parser.add_argument(
        "input",
        nargs="*",
        type=argparse.FileType("r", encoding="utf-8", errors="replace"),
        help="log file(s) to parse; reads stdin when omitted",
    )
    parser.add_argument(
        "-o", "--output", type=Path, help="output file (default depends on --csv)"
    )
    parser.add_argument(
        "--csv",
        action="store_true",
        help="write a CSV file instead of an Excel workbook",
    )
    args = parser.parse_args()

    records = parse_streams(args.input if args.input else [sys.stdin])
    if not records:
        parser.error("no PERF_DATA records found")

    # CSV is not a multi-sheet format. The file name is fixed to perf_data.csv, and
    # Excel imports it into a sheet named after the CSV file (PERF_DATA by default).
    output = args.output or Path("perf_data.csv" if args.csv else "perf_data.xlsx")
    if args.csv:
        write_csv(records, output)
    else:
        write_excel(records, output)

    print(f"Wrote {len(records)} record(s) to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
