---
name: api-coverage-checker
description: >-
  Analyze CCU API documentation coverage for the asc-comm repository.
  Use this skill whenever the user explicitly asks to generate an API coverage report,
  check documentation completeness, identify undocumented APIs, or analyze interface
  gaps between code and docs. Trigger only on explicit commands like "生成覆盖度报告",
  "分析API覆盖度", "检查接口文档完整性", "generate coverage report".
---

# API Coverage Checker

Analyze the asc-comm repository to compare public CCU API declarations in
`include/ccu` headers against documented APIs in `docs/api/ccu` markdown files.
Generate a detailed gap analysis as both xlsx and markdown.

Default scope is CCU only. Do not analyze other asc-comm API families unless the
user explicitly asks for a broader report.

## Workflow

### Step 1: Locate the repository

Use the current asc-comm checkout when available. The expected repository root
contains both `include/ccu` and `docs/api/ccu`.

### Step 2: Extract code APIs

Run the extraction script that parses public CCU headers from `include/ccu`:

```bash
python <skill-dir>/scripts/extract_code_apis.py \
  --include /mnt/workspace/gitCode/cann/asc-comm/include/ccu \
  --output /tmp/api-cov-output/code_apis.json
```

This produces a JSON file listing every API declaration found, including:
- `api_name`: cleaned function/method/class name
- `module`: always `ccu`
- `category`: CCU functional category (resource_allocation_operation, arg_load_store, data_movement, synchronization, execution_control, host_launch, etc.)
- `signature`: function signature string (return type + name + parameters)
- `file`: source header file path
- `npu_arch`: detected architecture support (`#if __NPU_ARCH__` guards)

### Step 3: Extract documented APIs

Run the extraction script that parses `docs/api/ccu` markdown documentation:

```bash
python <skill-dir>/scripts/extract_doc_apis.py \
  --docs /mnt/workspace/gitCode/cann/asc-comm/docs/api/ccu \
  --output /tmp/api-cov-output/doc_apis.json
```

This produces a JSON file listing every documented API, including:
- `api_name`: API name from filename or heading
- `module`: documentation module
- `supported_archs`: NPU_ARCH info from product support tables
- `doc_file`: source markdown file path

### Step 4: Generate the comparison report

Run the comparison and report generation script:

```bash
python <skill-dir>/scripts/generate_report.py \
  --code /tmp/api-cov-output/code_apis.json \
  --docs /tmp/api-cov-output/doc_apis.json \
  --out-xlsx <output-dir>/API覆盖缺口明细.xlsx \
  --out-md <output-dir>/API覆盖度分析报告.md
```

This produces:
1. **xlsx report** with 3 sheets: summary, missing API list with signatures, category stats
2. **md report** with overall coverage metrics and gap analysis

### Step 5: Report results to the user

Summarize the key findings:
- Total code APIs vs. documented APIs
- CCU coverage rate
- Top gap areas with example missing APIs
- Output file paths

## Dependencies

The scripts require:
- Python 3.7+
- `openpyxl` (install with `pip install openpyxl` if missing)

## What NOT to do

- Do NOT modify the source repository. All analysis is read-only.
- Do NOT generate the report without running the extraction steps first.
