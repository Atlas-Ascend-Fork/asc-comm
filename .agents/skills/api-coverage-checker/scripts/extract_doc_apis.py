"""
Extract documented CCU APIs from docs/api/ccu markdown files.

Usage:
    python extract_doc_apis.py --docs <repo>/docs/api/ccu --output <json-path>
"""
import argparse
import json
import os
import re
from pathlib import Path

MODULE_MAP = {
    "ccu": "ccu",
    "resource_allocation_operation": "ccu",
    "arg_load_store": "ccu",
    "data_movement": "ccu",
    "synchronization": "ccu",
    "execution_control": "ccu",
    "SIMD-API": "basic_api",
    "SIMT-API": "simt_api",
    "Utils-API": "utils",
    "c_api": "c_api",
    "tensor_api": "tensor_api",
    "AI-CPU-API": "aicpu_api",
    "appendix": "appendix",
}

NON_API_NAMES = {
    "readme", "index", "overview", "api_list", "list", "summary",
    "intro", "introduction", "guide", "manual", "reference",
    "toc", "contents", "changelog", "release_notes",
}

NON_API_PATTERNS = [
    r'^\d',                  # pure numbers
    r'(概述|简介|说明|约束|附录|概念|概览|列表|通用|变更|预留)',
    r'(SIMD-API|SIMT-API|Utils-API|API|C-API|README|AI-CPU)',
    r'(编程模型|编程指南|内存空间|内置变量|内置宏|内置数据|函数执行|扩展语法|核函数配置)',
    r'(Layout|层次化)',
    r'(数据搬运|数据搬运简介|数学函数)',
    r'(基础API|高阶API|基础数据结构|其他数据类型|C\+标准库)',
    r'(接口分类|常用操作)',
    r'README',
]

CCU_CATEGORY_MAP = {
    "resource_allocation_operation": "resource_allocation_operation",
    "arg_load_store": "arg_load_store",
    "data_movement": "data_movement",
    "synchronization": "synchronization",
    "execution_control": "execution_control",
}


def is_api_name(name):
    """Check if a name looks like an API entry rather than a guide page."""
    name_lower = name.lower()
    if name_lower in NON_API_NAMES:
        return False
    for pat in NON_API_PATTERNS:
        if re.search(pat, name):
            return False
    return True


def extract_npu_arch_from_doc(content):
    """Extract NPU_ARCH support from markdown product support tables."""
    archs = set()
    for npu in re.findall(r'<!--\s*npu="([^"]+)"', content):
        archs.add(npu)
    if "Ascend 950" in content:
        archs.add("950")
    if "Atlas 350" in content:
        archs.add("351x")
    if "Atlas A3 训练" in content or "Atlas A3 推理" in content:
        archs.add("220x")
    if "Atlas A2 训练" in content or "Atlas A2 推理" in content:
        archs.add("220x")
    if "Atlas 200I" in content or "A2 推理产品" in content:
        archs.add("300x")
    if "Atlas 推理系列" in content and "AI Core" in content:
        archs.add("200x")
    if "Atlas 训练系列" in content:
        archs.add("100x")

    if not archs:
        return None
    return ",".join(sorted(archs))


def extract_signature_from_doc(content):
    """Try to extract function signature from markdown doc."""
    # Look for 函数原型 section
    proto_match = re.search(r'函\s*数\s*原\s*型\s*\n+```(?:cpp|c\+\+)?\s*\n(.*?)```', content, re.S | re.DOTALL)
    if proto_match:
        proto = ' '.join(proto_match.group(1).strip().splitlines())
        return proto[:200]
    # Look for function signature in C API docs
    sig_match = re.search(r'(?:inline\s+)?(\w+\s+)(asc_\w+)\s*\(([^)]*)\)', content)
    if sig_match:
        return sig_match.group(0).strip()[:200]
    # Look for class declarations
    class_match = re.search(r'(?:class|struct)\s+(\w+)', content)
    if class_match:
        return f"class {class_match.group(1)}"
    return None


def clean_api_name(filename):
    """Clean an API name from a markdown filename."""
    name = os.path.splitext(filename)[0]
    # Remove trailing numbers like "-292", "-148", "-269"
    name = re.sub(r'-\d+$', '', name)
    # Remove trailing "1" from Utils-API names like "get1", "abs1", "sqrt1"
    if re.match(r'^\w+\d$', name) and name[-1] == '1':
        name = name[:-1]
    # Remove trailing underscore numbers
    name = re.sub(r'_\d+$', '', name)
    return name


def find_md_files(docs_dir):
    """Walk docs/api/ccu and yield (file_path, module_name, relative_path, content)."""
    base = Path(docs_dir)
    if not base.exists():
        return

    for f in sorted(base.rglob("*.md")):
        try:
            with open(f, "r", encoding="utf-8", errors="replace") as fh:
                content = fh.read()
        except Exception:
            content = ""

        rel = str(f.relative_to(base))

        # Determine module
        top_dir = rel.split("/")[0] if "/" in rel else ""
        module = "ccu" if base.name == "ccu" else MODULE_MAP.get(top_dir, "other")

        yield str(f), module, rel, content


def main():
    parser = argparse.ArgumentParser(description="Extract documented APIs from docs/api/")
    parser.add_argument("--docs", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    apis = []
    file_count = 0
    non_api_count = 0

    for file_path, module, rel_path, content in find_md_files(args.docs):
        file_count += 1
        fname = os.path.basename(file_path)
        api_name = clean_api_name(fname)

        if not is_api_name(api_name):
            non_api_count += 1
            continue

        signature = extract_signature_from_doc(content)
        npu_arch = extract_npu_arch_from_doc(content)
        top_dir = rel_path.split("/")[0] if "/" in rel_path else ""
        category = CCU_CATEGORY_MAP.get(top_dir, module)

        apis.append({
            "api_name": api_name,
            "module": module,
            "category": category,
            "doc_file": rel_path,
            "signature": signature,
            "npu_arch": npu_arch,
        })

    # Deduplicate
    seen = set()
    unique = []
    for api in apis:
        key = api["api_name"]
        if key not in seen:
            seen.add(key)
            unique.append(api)

    result = {
        "file_count": file_count,
        "non_api_files": non_api_count,
        "api_count": len(unique),
        "apis": unique,
        "by_module": {},
    }

    for api in unique:
        mod = api["module"]
        result["by_module"].setdefault(mod, 0)
        result["by_module"][mod] += 1

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)

    print(f"Extracted {len(unique)} documented APIs from {file_count} files ({non_api_count} non-API skipped)")
    print(f"Modules: {json.dumps(result['by_module'], ensure_ascii=False)}")


if __name__ == "__main__":
    main()
