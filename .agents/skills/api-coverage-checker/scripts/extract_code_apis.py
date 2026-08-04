"""
Extract public CCU API declarations from include/ccu header files.

Usage:
    python extract_code_apis.py --include <repo>/include/ccu --output <json-path>
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

MODULE_MAP = {
    "ccu": "ccu",
    "ain": "ain",
    "aicore": "aicore",
}

CCU_PUBLIC_MACROS = {"CCU_IF", "CCU_ELSE", "CCU_WHILE", "CCU_DO"}
CCU_INTERNAL_TYPES = {
    "Arg",
    "NoAllocTag",
    "CcuArithmeticOperator",
    "CcuArithmeticOperatorType",
    "CcuException",
    "CcuOperator",
    "CondExpr",
}

CCU_API_CATEGORY_BY_NAME = {
    "Variable": "resource_allocation_operation",
    "Address": "resource_allocation_operation",
    "Event": "resource_allocation_operation",
    "CcuBuffer": "resource_allocation_operation",
    "LocalAddr": "resource_allocation_operation",
    "RemoteAddr": "resource_allocation_operation",
    "Array": "resource_allocation_operation",
    "GetResByChannel": "resource_allocation_operation",
    "LoadArg": "arg_load_store",
    "Load": "arg_load_store",
    "Store": "arg_load_store",
    "LocalCopy": "data_movement",
    "LocalReduce": "data_movement",
    "Read": "data_movement",
    "ReadReduce": "data_movement",
    "Write": "data_movement",
    "WriteReduce": "data_movement",
    "EventRecord": "synchronization",
    "EventWait": "synchronization",
    "LocalNotifyRecord": "synchronization",
    "LocalNotifyWait": "synchronization",
    "NotifyRecord": "synchronization",
    "NotifyWait": "synchronization",
    "WriteVariableWithNotify": "synchronization",
    "CCU_IF": "execution_control",
    "CCU_ELSE": "execution_control",
    "CCU_WHILE": "execution_control",
    "CCU_DO": "execution_control",
    "Loop": "execution_control",
    "LoopGroup": "execution_control",
    "Func": "execution_control",
    "CallFunc": "execution_control",
}

def find_module(rel_path):
    rel = rel_path.replace("\\", "/")
    top_dir = rel.split("/")[0]
    return MODULE_MAP.get(top_dir, "other")


def find_category(rel_path, fname, content):
    """Infer functional category from path, file name, and content."""
    p = rel_path.lower()
    fn = fname.lower()
    if "ccu_variable" in fn or "ccu_address" in fn or "ccu_event" in fn or "ccu_buffer" in fn:
        return "resource_allocation_operation"
    if "ccu_local_addr" in fn or "ccu_remote_addr" in fn or "ccu_array" in fn:
        return "resource_allocation_operation"
    if "ccu_primitives" in fn:
        return "data_movement"
    if "ccu_func" in fn or "ccu_loop" in fn or "control_flow_macro" in fn:
        return "execution_control"
    if "ccu_launch" in fn or "host_launch" in fn:
        return "host_launch"
    if "ccu_res" in fn or "ccu_types" in fn or "ccu_utils" in fn:
        return "ccu_support"
    if "vector_compute" in p or "vec_" in fn:
        return "vector_compute"
    if "cube_compute" in p or "mm_" in fn or "mmad" in fn:
        return "cube_compute"
    if "datamove" in p or "data_copy" in fn or "copy_" in fn or "loaddata" in fn:
        return "data_move"
    if "sync" in p or "sync" in fn:
        return "sync"
    if "scalar" in p or "scalar" in fn:
        return "scalar"
    if "sys_var" in p:
        return "sys_var"
    if "activation" in p or "gelu" in fn or "softmax" in fn or "sigmoid" in fn:
        return "activation"
    if "math" in p:
        return "math"
    if "matmul" in p or "matmul" in fn:
        return "matmul"
    if "conv" in p and "conv" in fn:
        return "conv"
    if "reduce" in p:
        return "reduce"
    if "normalization" in p or "norm" in fn or "rmsnorm" in fn:
        return "normalization"
    if "quant" in p or "quant" in fn or "dequant" in fn:
        return "quantization"
    if "hccl" in p or "hcomm" in p:
        return "hccl"
    if "sort" in p or "topk" in fn:
        return "sort"
    if "pad" in p or "broadcast" in fn:
        return "pad"
    if "transpose" in p or "transdata" in fn:
        return "transpose"
    if "filter" in p or "dropout" in fn:
        return "filter"
    if "select" in p:
        return "select"
    if "index" in p:
        return "index"
    if "arch" in p or "tensor" in fn:
        return "tensor"
    if "utils" in p or "debug" in p or "log" in p:
        return "utils"
    if "reg_compute" in p:
        return "vector_compute"
    return "other"


def extract_npu_arch(content):
    """Extract NPU_ARCH info from #if guards in source code."""
    archs = set()
    # Look for __NPU_ARCH__ values
    matches = re.findall(r'__NPU_ARCH__\s*==\s*(\d+)', content)
    for m in matches:
        archs.add(m + "x")
    # Look for arch-specific sections
    if "ascend_310" in content or "dav-1001" in content:
        archs.add("100x")
    if "dav-2002" in content:
        archs.add("200x")
    if "dav-2201" in content:
        archs.add("220x")
    if "dav-3002" in content:
        archs.add("300x")
    if "dav-3510" in content or "Atlas 350" in content:
        archs.add("351x")

    if not archs:
        # Infer from file location
        return None
    return ",".join(sorted(archs))


def extract_c_api_apis(file_path, rel_path, content, apis):
    """Extract asc_ prefixed C API functions."""
    # Find asc_ function declarations
    for match in re.finditer(
        r'(?:inline\s+)?(\w+(?:\s*\*)?)\s+(asc_\w+)\s*\(([^)]*)\)',
        content, re.MULTILINE
    ):
        ret_type, name, params = match.groups()
        sig = f"{ret_type.strip()} {name}({params.strip()})"
        apis.append({
            "api_name": name,
            "module": "c_api",
            "category": find_category(rel_path, os.path.basename(file_path), content),
            "signature": sig[:200],
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })


def extract_basic_apis(file_path, rel_path, content, apis):
    """Extract __aicore__ function declarations from basic_api headers."""
    # Pattern: __aicore__ inline return_type func_name(...)
    for match in re.finditer(
        r'__aicore__\s+inline\s+([\w\s*&:<>]+?)\s+(\w+)\s*\(([^;]*?)\)\s*[{;]',
        content, re.MULTILINE
    ):
        ret_type, name, params = match.groups()
        ret_type = ' '.join(ret_type.strip().split()[-3:])
        sig = f"{ret_type} {name}({params.strip()})"
        apis.append({
            "api_name": name,
            "module": "basic_api",
            "category": find_category(rel_path, os.path.basename(file_path), content),
            "signature": sig[:200],
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })


def extract_simt_apis(file_path, rel_path, content, apis):
    """Extract SIMT API functions."""
    # Builtin: __float2int_rn, __half2float, etc.
    for match in re.finditer(
        r'(?:inline\s+)?(\w+(?:\s*\*)?)\s+(__\w+)\s*\(([^)]*)\)',
        content, re.MULTILINE
    ):
        ret_type, name, params = match.groups()
        if name.startswith('_Z') or len(name) < 5:
            continue
        sig = f"{ret_type.strip()} {name}({params.strip()})"
        apis.append({
            "api_name": name,
            "module": "simt_api",
            "category": find_category(rel_path, os.path.basename(file_path), content),
            "signature": sig[:200],
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })
    # atomic*, make_*, math functions
    for match in re.finditer(
        r'(?:inline\s+)?(\w+(?:\s*\*)?)\s+(make_\w+|atomic\w+|__syncthreads|__syncwarp|asc_vf_call)\s*\([^)]*\)',
        content, re.MULTILINE
    ):
        ret_type, name = match.groups()
        sig = f"{ret_type.strip()} {name}(...)"
        apis.append({
            "api_name": name,
            "module": "simt_api",
            "category": "sync" if "sync" in name else "math",
            "signature": sig[:200],
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })


def extract_adv_apis(file_path, rel_path, content, apis):
    """Extract advanced API class/function names."""
    fname = os.path.basename(file_path)
    if fname.endswith('.h'):
        name = os.path.splitext(fname)[0]
        if name not in ('kernel_api', 'tiling_api', 'common', 'internal'):
            # Use filename as API name for operator headers
            apis.append({
                "api_name": name,
                "module": "adv_api",
                "category": find_category(rel_path, fname, content),
                "signature": f"class/template {name}",
                "file": rel_path,
                "npu_arch": extract_npu_arch(content),
            })

    # Also extract __aicore__ functions inside adv_api
    for match in re.finditer(
        r'__aicore__\s+inline\s+([\w\s*&:<>]+?)\s+(\w+)\s*\(([^;]*?)\)\s*[{;]',
        content, re.MULTILINE
    ):
        ret_type, name, params = match.groups()
        ret_type = ' '.join(ret_type.strip().split()[-3:])
        sig = f"{ret_type} {name}({params.strip()})"
        apis.append({
            "api_name": name,
            "module": "adv_api",
            "category": find_category(rel_path, fname, content),
            "signature": sig[:200],
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })


def extract_tensor_apis(file_path, rel_path, content, apis):
    """Extract tensor API declarations."""
    for match in re.finditer(
        r'(?:inline\s+)?(\w+(?:\s*\*)?)\s+(\w+)\s*\(([^)]*)\)',
        content, re.MULTILINE
    ):
        ret_type, name, params = match.groups()
        if name.startswith('_'):
            continue
        sig = f"{ret_type.strip()} {name}({params.strip()})"
        apis.append({
            "api_name": name,
            "module": "tensor_api",
            "category": "tensor",
            "signature": sig[:200],
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })


def extract_utils_apis(file_path, rel_path, content, apis):
    """Extract utility function declarations."""
    for match in re.finditer(
        r'(?:inline\s+)?(\w+(?:\s*\*)?)\s+(asc_\w+|\w+)\s*\(([^)]*)\)',
        content, re.MULTILINE
    ):
        ret_type, name, params = match.groups()
        if name.startswith('_') or name in ('if', 'while', 'for', 'return', 'sizeof'):
            continue
        sig = f"{ret_type.strip()} {name}({params.strip()})"
        apis.append({
            "api_name": name,
            "module": "utils",
            "category": "utils",
            "signature": sig[:200],
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })


def extract_ccu_apis(file_path, rel_path, content, apis):
    """Extract public CCU classes, macros, and free functions."""
    fname = os.path.basename(file_path)
    category = find_category(rel_path, fname, content)

    for match in re.finditer(r'\b(?:class|struct)\s+([A-Z]\w*)(?:\s+final)?\s*\{', content):
        name = match.group(1)
        prefix = content[max(0, match.start() - 10):match.start()].strip()
        if prefix.endswith("enum") or name.endswith("Traits") or name in CCU_INTERNAL_TYPES:
            continue
        apis.append({
            "api_name": name,
            "module": "ccu",
            "category": CCU_API_CATEGORY_BY_NAME.get(name, category),
            "signature": f"class {name}",
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })

    for match in re.finditer(r'^\s*#\s*define\s+(CCU_\w+)\b', content, re.MULTILINE):
        name = match.group(1)
        if name not in CCU_PUBLIC_MACROS:
            continue
        apis.append({
            "api_name": name,
            "module": "ccu",
            "category": CCU_API_CATEGORY_BY_NAME.get(name, "execution_control"),
            "signature": f"#define {name}",
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })

    for match in re.finditer(
        r'\binline\s+([\w\s*&:<>]+?)\s+([A-Z]\w*)\s*\(([^;{}]*?)\)\s*(?:const\s*)?[{;]',
        content,
        re.MULTILINE | re.DOTALL,
    ):
        ret_type, name, params = match.groups()
        if name.startswith("Ccu") or name.startswith("_Ccu"):
            continue
        ret_type = " ".join(ret_type.strip().split())
        params = " ".join(params.strip().split())
        apis.append({
            "api_name": name,
            "module": "ccu",
            "category": CCU_API_CATEGORY_BY_NAME.get(name, category),
            "signature": f"{ret_type} {name}({params})"[:200],
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })

    for match in re.finditer(
        r'\bextern\s+([\w\s*&:<>]+?)\s+(HcommCcu\w+)\s*\(([^;{}]*?)\)\s*[^;]*;',
        content,
        re.MULTILINE | re.DOTALL,
    ):
        ret_type, name, params = match.groups()
        ret_type = " ".join(ret_type.strip().split())
        params = " ".join(params.strip().split())
        apis.append({
            "api_name": name,
            "module": "ccu",
            "category": "host_launch",
            "signature": f"{ret_type} {name}({params})"[:200],
            "file": rel_path,
            "npu_arch": extract_npu_arch(content),
        })


EXTRACTORS = {
    "ccu": [extract_ccu_apis],
    "ain": [extract_utils_apis],
    "aicore": [extract_utils_apis],
    "c_api": [extract_c_api_apis],
    "basic_api": [extract_basic_apis],
    "adv_api": [extract_adv_apis],
    "simt_api": [extract_simt_apis],
    "tensor_api": [extract_tensor_apis],
    "utils": [extract_utils_apis],
    "aicpu_api": [extract_utils_apis],
}


def find_headers(include_dir):
    """Walk include/ccu and yield (file_path, module_name, relative_path)."""
    base = Path(include_dir)
    if not base.exists():
        return

    for f in sorted(base.rglob("*")):
        if f.suffix not in {".h", ".hpp"}:
            continue
        if "impl" in f.name or "/hcomm/" in f.as_posix():
            continue
        try:
            with open(f, "r", encoding="utf-8", errors="replace") as fh:
                content = fh.read()
        except Exception:
            content = ""
        rel = str(f.relative_to(base))
        module = "ccu" if base.name == "ccu" else find_module(rel)
        yield str(f), module, rel, content


def main():
    parser = argparse.ArgumentParser(description="Extract public CCU API declarations from include/ccu")
    parser.add_argument("--include", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    apis = []
    file_count = 0

    for file_path, module, rel_path, content in find_headers(args.include):
        file_count += 1
        extractors = EXTRACTORS.get(module, [extract_utils_apis])
        for extractor in extractors:
            extractor(file_path, rel_path, content, apis)

    # Deduplicate by name+signature
    seen = set()
    unique = []
    for api in apis:
        key = (api["api_name"], api["signature"][:100])
        if key not in seen:
            seen.add(key)
            unique.append(api)

    result = {
        "file_count": file_count,
        "api_count": len(unique),
        "apis": unique,
        "by_module": {},
        "by_category": {},
    }

    for api in unique:
        mod = api["module"]
        cat = api["category"]
        result["by_module"].setdefault(mod, 0)
        result["by_module"][mod] += 1
        result["by_category"].setdefault(cat, 0)
        result["by_category"][cat] += 1

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)

    print(f"Extracted {len(unique)} unique APIs from {file_count} headers")
    print(f"Modules: {json.dumps(result['by_module'], ensure_ascii=False)}")
    print(f"Categories: {json.dumps(result['by_category'], ensure_ascii=False)}")


if __name__ == "__main__":
    main()
