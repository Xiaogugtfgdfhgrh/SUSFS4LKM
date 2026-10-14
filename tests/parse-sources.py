#!/usr/bin/env python3
import ast
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
for path in sorted((root / "tools").rglob("*.py")) + sorted((root / "tests").rglob("*.py")):
    ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
print("PY_PARSE_OK")
