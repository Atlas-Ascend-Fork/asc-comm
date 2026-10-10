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
import sys
from pathlib import Path

import numpy as np


MAX_RANKS = 16


def parse_args():
    parser = argparse.ArgumentParser(
        description="Verify CCU AllGather output against golden data."
    )
    parser.add_argument("--rank-size", type=int, required=True)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("./output"))
    parser.add_argument("--golden", type=Path, default=Path("./output/golden.bin"))
    return parser.parse_args()


def main():
    args = parse_args()
    if not 1 <= args.rank_size <= MAX_RANKS:
        raise ValueError(f"rank-size must be in [1, {MAX_RANKS}]")
    if args.count < 0:
        raise ValueError("count must be non-negative")

    expected_size = args.rank_size * args.count
    golden = np.fromfile(args.golden, dtype=np.float32)
    if golden.size != expected_size:
        raise ValueError(
            f"golden size mismatch: expected {expected_size}, got {golden.size}"
        )

    for rank_id in range(args.rank_size):
        output_path = args.output_dir / f"output_rank_{rank_id}.bin"
        output = np.fromfile(output_path, dtype=np.float32)
        if output.size != expected_size:
            raise ValueError(
                f"rank {rank_id} output size mismatch: "
                f"expected {expected_size}, got {output.size}"
            )
        mismatch = np.flatnonzero(output != golden)
        if mismatch.size != 0:
            index = int(mismatch[0])
            raise ValueError(
                f"rank {rank_id} mismatch at {index}: "
                f"expected {golden[index]}, got {output[index]}"
            )
        print(f"rank {rank_id} verify pass")

    print("test pass!")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"[ERROR] {error}")
        sys.exit(1)
