#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

RISCV_TOOLCHAIN_BIN=${RISCV_TOOLCHAIN_BIN:-/home/vash/.cache/tangcore-dev/toolchain/bin}
GOWIN_EDA_BIN=${GOWIN_EDA_BIN:-/home/vash/tools/gowin-1.9.11.03/IDE/bin}

export PATH="$RISCV_TOOLCHAIN_BIN:$GOWIN_EDA_BIN:$PATH"
export PYTHONPATH="$PROJECT_ROOT/third_party/migen:$PROJECT_ROOT/third_party/litex:$PROJECT_ROOT/third_party/litedram:$PROJECT_ROOT/third_party/litepcie:$PROJECT_ROOT/third_party/litex-boards:$PROJECT_ROOT/third_party/pythondata-software-picolibc:$PROJECT_ROOT/third_party/pythondata-software-compiler_rt${PYTHONPATH:+:$PYTHONPATH}"

# Gowin 1.9.11.03 bundles an older FreeType while current host Fontconfig is
# linked against a newer ABI.  Preloading the matching host library is the
# proven headless setup used by Tang-Phosphor on this workstation.
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
GOWIN_HOST_FREETYPE=/usr/lib/x86_64-linux-gnu/libfreetype.so.6
if [[ -f "$GOWIN_HOST_FREETYPE" ]]; then
    export LD_PRELOAD="$GOWIN_HOST_FREETYPE${LD_PRELOAD:+:$LD_PRELOAD}"
fi

command -v riscv64-unknown-elf-gcc >/dev/null
command -v gw_sh >/dev/null
