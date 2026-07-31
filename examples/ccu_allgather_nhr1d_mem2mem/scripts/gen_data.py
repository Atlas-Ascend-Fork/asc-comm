#!/usr/bin/env python
# -*- coding: UTF-8 -*-
# ----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------------------------------------

import argparse
from pathlib import Path

import numpy as np


MAX_RANKS = 16


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate FP32 input and golden data for CCU AllGather."
    )
    parser.add_argument("--rank-size", type=int, required=True)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--input-dir", type=Path, default=Path("./input"))
    parser.add_argument("--output-dir", type=Path, default=Path("./output"))
    return parser.parse_args()


def main():
    args = parse_args()
    if not 1 <= args.rank_size <= MAX_RANKS:
        raise ValueError(f"rank-size must be in [1, {MAX_RANKS}]")
    if args.count < 0:
        raise ValueError("count must be non-negative")

    args.input_dir.mkdir(parents=True, exist_ok=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for path in args.input_dir.glob("input_rank_*.bin"):
        path.unlink()
    for path in args.output_dir.glob("output_rank_*.bin"):
        path.unlink()

    rank_inputs = []
    indices = np.arange(args.count, dtype=np.uint64)
    for rank_id in range(args.rank_size):
        data = (rank_id * 1000000 + indices + 1).astype(np.float32)
        data.tofile(args.input_dir / f"input_rank_{rank_id}.bin")
        rank_inputs.append(data)

    golden = np.concatenate(rank_inputs)
    golden.tofile(args.output_dir / "golden.bin")
    print(
        f"generated {args.rank_size} rank inputs and golden data, "
        f"{args.count} FP32 elements per rank"
    )


if __name__ == "__main__":
    main()
