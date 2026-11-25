#!/usr/bin/env bash
# Zips dist/AtelierAlchemy into a mod-manager friendly archive (Data/ layout).
set -euo pipefail
cd "$(dirname "$0")/.."
version=$(sed -n 's/^\s*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)
out="dist/AtelierAlchemy-${version}.zip"
python3 - "$out" <<'PY'
import sys, zipfile, pathlib
root = pathlib.Path("dist/AtelierAlchemy")
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(root.rglob("*")):
        if f.is_file():
            z.write(f, f.relative_to(root))
print("wrote", sys.argv[1])
PY
