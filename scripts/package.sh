#!/usr/bin/env bash
# Builds the release archives in dist/:
#   AtelierAlchemy-<version>.zip         the mod, mod-manager friendly (Data/ layout)
#   AtelierAlchemy-<version>-source.zip  the source code (GPL-3), everything git tracks or would
#                                        track (uncommitted edits included, .gitignore respected)
# With --with-deps the source archive also contains the dependency sources CMake fetched
# (CommonLibSSE-NG, spdlog, Dear ImGui, ...; run a build first), for a fully self-contained
# GPL source release.
set -euo pipefail
cd "$(dirname "$0")/.."

with_deps=0
for arg in "$@"; do
	case "$arg" in
	--with-deps) with_deps=1 ;;
	*) echo "usage: $0 [--with-deps]" >&2; exit 2 ;;
	esac
done

version=$(sed -n 's/^\s*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)

python3 - "$version" "$with_deps" <<'PY'
import pathlib
import subprocess
import sys
import zipfile

version, with_deps = sys.argv[1], sys.argv[2] == "1"
dist = pathlib.Path("dist")

# The mod.
mod = dist / f"AtelierAlchemy-{version}.zip"
root = dist / "AtelierAlchemy"
with zipfile.ZipFile(mod, "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(root.rglob("*")):
        if f.is_file():
            z.write(f, f.relative_to(root))
print("wrote", mod)

# The source code.
listing = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
                         check=True, capture_output=True).stdout
files = sorted(pathlib.Path(p) for p in listing.decode().split("\0") if p and pathlib.Path(p).is_file())
prefix = pathlib.Path(f"AtelierAlchemy-{version}-source")
source = dist / f"AtelierAlchemy-{version}-source.zip"
with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED) as z:
    for f in files:
        z.write(f, prefix / f)
    deps = 0
    if with_deps:
        fetched = sorted(pathlib.Path("build").glob("*/_deps/*-src"))
        if not fetched:
            sys.exit("--with-deps: no fetched dependencies under build/*/_deps - run ./scripts/build.sh first")
        # CommonLib's OpenVR submodule (the SteamVR SDK, ~340 MB of binaries) is only used for
        # Skyrim VR, which this mod doesn't build; its headers and license are kept.
        def skipped(dep_name, rel):
            parts = rel.parts
            if dep_name == "commonlibsse-src" and parts[:2] == ("extern", "openvr"):
                return not (len(parts) > 2 and (parts[2] == "headers" or parts[2].upper().startswith("LICENSE")))
            return False

        seen = set()
        for dep in fetched:
            if dep.name in seen:  # same dependency in several build presets
                continue
            seen.add(dep.name)
            for f in sorted(dep.rglob("*")):
                rel = f.relative_to(dep)
                if not f.is_file() or ".git" in rel.parts or skipped(dep.name, rel):
                    continue
                z.write(f, prefix / "third_party" / dep.name / rel)
            deps += 1
print("wrote", source, f"({len(files)} files" + (f", {deps} dependencies)" if with_deps else ")"))
PY
