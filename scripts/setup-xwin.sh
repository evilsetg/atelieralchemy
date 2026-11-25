#!/usr/bin/env bash
# Downloads the MSVC CRT + Windows SDK headers/libs into .xwin/ using xwin.
# Running this means you accept the Microsoft Visual Studio license terms:
#   https://go.microsoft.com/fwlink/?LinkId=2086102
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .xwin/cache .xwin/sysroot
xwin --accept-license --http-retry 10 --cache-dir .xwin/cache --sdk-version 10.0.22621 splat \
  --output .xwin/sysroot --use-winsysroot-style --preserve-ms-arch-notation
echo "Sysroot ready at $(pwd)/.xwin/sysroot"
