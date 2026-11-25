#!/usr/bin/env bash
# One-shot build: regenerate assets, validate data, run core tests, build the DLL and
# stage everything in dist/AtelierAlchemy. Run inside `nix develop` (or with the same tools).
set -euo pipefail
cd "$(dirname "$0")/.."

[ -d "${XWIN_SYSROOT:-.xwin/sysroot}" ] || ./scripts/setup-xwin.sh

python3 tools/build_assets.py
python3 tools/validate_data.py
./scripts/test-core.sh
cmake --preset linux-clangcl
cmake --build --preset linux-clangcl
echo "Mod staged in dist/AtelierAlchemy"
