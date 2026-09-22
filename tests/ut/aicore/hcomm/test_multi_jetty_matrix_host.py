#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
"""Exercise real matrix build/run scripts with local compiler/device substitutes.

No target scripts are generated or deployed. This does not verify CANN or DMA.
"""

from pathlib import Path
import itertools
import json
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
EXAMPLE = ROOT / "examples/aicore/hcomm/07_multi_jetty_batch_perf"
BATCHES = (1, 8, 32, 64, 128, 256)


def call(args, env, success=True):
    result = subprocess.run(args, env=env, text=True, capture_output=True)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result.stdout + result.stderr


with tempfile.TemporaryDirectory(prefix="multi-jetty-matrix-") as temporary:
    root = Path(temporary)
    for name in ("build.sh", "run.sh"):
        (root / name).write_text(
            (EXAMPLE / name).read_text(encoding="utf-8"), encoding="utf-8"
        )
        subprocess.run(["bash", "-n", str(root / name)], check=True)
    binary_source = root / "mock_device.py"
    binary_source.write_text(
        """#!/usr/bin/env python3
from pathlib import Path
import json,os,sys
batch=int(Path(__file__).parent.name.split('bs')[-1])
if sys.argv[1:]==['--build-info']:
    version=os.environ.get('MOCK_AUDIT','matrix-v4')
    print(f'BUILD_CONFIG | Backend=simt | Layout={version} | CompletionVersion=1 | MatrixBatch={batch}')
    sys.exit(0)
rank,nranks,root,api,iterations,warmup,payload,bs,jettys=sys.argv[1:]
if os.environ.get('MOCK_ABORT')=='1': sys.exit(124)
assert nranks=='2' and iterations=='1024' and payload=='64'
assert int(bs)==batch and int(warmup)==(96+batch-1)//batch*batch
assert api in ['write','read','notify'] and int(jettys) in [1,2,4,8]
if rank=='0':
    with open(os.environ['MOCK_CASES'],'a') as f:f.write(json.dumps([api,batch,int(jettys)])+'\\n')
    print('COMPLETION_CHECK | Status=PASS')
    print(f'PERF_DATA | API={api} | BatchSize={batch} | Jettys={jettys}')
if (rank=='0' and api=='read') or (rank=='1' and api!='read'):
    if os.environ.get('MOCK_MISSING')!='1': print('DATA_AUDIT_SUMMARY | CheckedRequests=1 | BadRequests=0')
if rank=='1':
    if api=='notify':print('NOTIFY_AUDIT_SUMMARY | BadNotifies=0')
    if os.environ.get('MOCK_FAIL')=='1' and api=='read' and jettys=='4':
        print('data mismatch: simulated failure')
        sys.exit(2)
""",
        encoding="utf-8",
    )
    cmake = root / "cmake"
    cmake.write_text(
        """#!/usr/bin/env python3
from pathlib import Path
import os,shutil,sys
a=sys.argv[1:]
if os.environ.get('MOCK_COMPILE_FAIL')=='1':
    print('compiler error: simulated configure failure');sys.exit(7)
if a[0]=='-S':
    build=Path(a[a.index('-B')+1]);build.mkdir(parents=True,exist_ok=True)
    batch=int(next(x.split('=')[1] for x in a if x.startswith('-DMULTI_JETTY_MATRIX_BATCH=')))
    (build/'batch').write_text(str(batch))
else:
    assert a[0]=='--build'
    build=Path(a[1]);batch=int((build/'batch').read_text());capacity=min(1024,max(32,batch*8))
    obj=build/'CMakeFiles/multi_jetty_batch_perf.dir/simt_kernel.cpp.o'
    obj.parent.mkdir(parents=True,exist_ok=True);obj.write_text('object')
    binary=build/'multi_jetty_batch_perf';shutil.copyfile(os.environ['MOCK_DEVICE'],binary);binary.chmod(0o755)
    stack=os.environ.get('MOCK_STACK','512')
    print(f'[BISHENG] SIMT VF Function properties for SimtParallelMultiJettyVfILj{batch}ELj{capacity}E: Stack size: {stack} bytes, Used register number: 120')
""",
        encoding="utf-8",
    )
    cmake.chmod(0o755)
    cases = root / "cases.jsonl"
    env = dict(
        os.environ,
        PATH=str(root) + os.pathsep + os.environ["PATH"],
        ASCEND_HOME_PATH=str(root),
        MOCK_DEVICE=str(binary_source),
        MOCK_CASES=str(cases),
    )
    for batch in BATCHES:
        build_args = ["bash", str(root / "build.sh"), "--batch-size", str(batch)]
        output = call(build_args, env)
        resource = (
            root / f"build/simt-matrix-bs{batch}/build-resource.txt"
        ).read_text()
        assert (
            f"VF={batch},{min(1024, max(32, batch * 8))}" in resource
            and "Registers=120" in resource
        )
        assert output.strip() == ""
        args = [
            "bash",
            str(root / "run.sh"),
            "--batch-size",
            str(batch),
            "--api",
            "all",
            "--jetty-counts",
            "1,2,4,8",
        ]
        output = call(args, env)
        assert "Cases=12 | Pass=12 | Fail=0 | Status=PASS" in output
        assert output.count("RESULT |") == 12
        assert all(
            line.startswith(("RESULT |", "MATRIX_SUMMARY |"))
            for line in output.splitlines()
        )
        metadata = list((root / "results").glob("*.log.metadata"))
        assert metadata and any("RUN_BINARY |" in p.read_text() for p in metadata)
        assert "configuration does not match" in call(
            args, dict(env, MOCK_AUDIT="9"), False
        )
    actual = [tuple(json.loads(line)) for line in cases.read_text().splitlines()]
    expected = set(
        itertools.product(("write", "read", "notify"), BATCHES, (1, 2, 4, 8))
    )
    assert len(actual) == 72 and set(actual) == expected
    output = call(args, dict(env, MOCK_FAIL="1"), False)
    assert "Cases=12 | Pass=11 | Fail=1 | Status=FAIL" in output
    assert "CASE_ERROR" in output
    assert "Status=FAIL" in call(args, dict(env, MOCK_MISSING="1"), False)
    assert "Cases=1 | Pass=0 | Fail=1 | Status=ABORTED" in call(
        args, dict(env, MOCK_ABORT="1"), False
    )
    simple = [
        "bash",
        str(root / "run.sh"),
        "--batch-size",
        "256",
        "--api",
        "write",
        "--jetty-counts",
        "1",
    ]
    assert "Cases=1 | Pass=1 | Fail=0 | Status=PASS" in call(simple, env)
    assert "Duplicate Jetty count" in call(simple[:-1] + ["1,1"], env, False)
    assert "exceeds 1152" in call(build_args, dict(env, MOCK_STACK="1153"), False)
    assert "no matching successful build" in call(args, env, False)

    assert "compiler error: simulated configure failure" in call(
        build_args, dict(env, MOCK_COMPILE_FAIL="1"), False
    )
    assert "no matching successful build" in call(args, env, False)

    # Exercise actual preprocessor selection: each artifact contains one VF call and no probe hook.
    source = (EXAMPLE / "simt_kernel.cpp").read_text(encoding="utf-8")
    source = re.sub(r"^\s*#include[^\n]*", "", source, flags=re.M)
    for batch in BATCHES:
        result = subprocess.run(
            [
                "g++",
                "-E",
                "-P",
                "-x",
                "c++",
                f"-DMULTI_JETTY_MATRIX_BATCH={batch}",
                "-DMULTI_JETTY_HCOMM_SIMT_HEADER=1",
                "-",
            ],
            input=source,
            text=True,
            capture_output=True,
            check=True,
        )
        assert result.stdout.count("asc_vf_call<") == 1
        assert re.search(r"SubmitPublisherCoreBatches\s*\(\s*hcomm\b", result.stdout)
print(
    "PASS: six one-VF selections, actual build/run scripts, 72 unique cases, failure summary and resource gates"
)
