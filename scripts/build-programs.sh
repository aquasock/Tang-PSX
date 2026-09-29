#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Build the programs under software/programs/<name>/ into DDR3-loadable images:
# build/programs/<name>/<name>.{elf,bin,tpx}. Each .tpx is ready for
# tools/ae350_run.py upload/run.

set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$PROJECT_ROOT/scripts/env.sh"

CC=riscv64-unknown-elf-gcc
OBJCOPY=riscv64-unknown-elf-objcopy
COMMON="$PROJECT_ROOT/software/programs/common"
CFLAGS=(-march=rv32imafdc -mabi=ilp32 -O2 -g -ffreestanding -fno-builtin
    -Wall -Wextra -Werror -I"$PROJECT_ROOT/software/common")
LDFLAGS=(-nostdlib -nostartfiles -T "$COMMON/linker.ld" -Wl,--no-relax -Wl,--gc-sections)

programs=("$@")
if (( ${#programs[@]} == 0 )); then
    for dir in "$PROJECT_ROOT"/software/programs/*/; do
        name=$(basename "$dir")
        [[ "$name" == common ]] || programs+=("$name")
    done
fi

for name in "${programs[@]}"; do
    src="$PROJECT_ROOT/software/programs/$name"
    out="$PROJECT_ROOT/build/programs/$name"
    extra_sources=()
    mkdir -p "$out"
    if [[ "$name" == blob ]]; then
        python3 "$PROJECT_ROOT/tools/ae350_run.py" blob -o "$out/blob.bin"
    fi
    if [[ "$name" == psx_diag ]]; then
        python3 "$PROJECT_ROOT/tools/gen_psx_vectors.py" -o "$out/psx_vectors.h"
        extra_sources=("$PROJECT_ROOT"/software/psx/*.c)
    fi
    "$CC" "${CFLAGS[@]}" -I"$PROJECT_ROOT/software/psx" -I"$out" \
        "${LDFLAGS[@]}" -Wa,-I"$out" -o "$out/$name.elf" \
        "$COMMON/crt0.S" "$src"/*.c "${extra_sources[@]}" \
        $(ls "$src"/*.S 2>/dev/null) -lgcc
    "$OBJCOPY" -O binary "$out/$name.elf" "$out/$name.bin"
    python3 "$PROJECT_ROOT/tools/ae350_run.py" pack "$out/$name.bin" -o "$out/$name.tpx"
done
