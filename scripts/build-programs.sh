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
    program_cflags=()
    program_includes=()
    link_libraries=(-lgcc)
    mkdir -p "$out"
    if [[ "$name" == psx_bios_lightrec ]]; then
        src="$PROJECT_ROOT/software/programs/psx_bios"
    fi
    if [[ "$name" == psx_disc_lightrec ]]; then
        src="$PROJECT_ROOT/software/programs/psx_disc"
    fi
    if [[ "$name" == blob ]]; then
        python3 "$PROJECT_ROOT/tools/ae350_run.py" blob -o "$out/blob.bin"
    fi
    if [[ "$name" == psx_diag ]]; then
        python3 "$PROJECT_ROOT/tools/gen_psx_vectors.py" -o "$out/psx_vectors.h"
        extra_sources=("$PROJECT_ROOT/software/psx/r3000.c"
            "$PROJECT_ROOT/software/psx/gte.c")
    fi
    if [[ "$name" == psx_bios || "$name" == psx_bios_lightrec ||
          "$name" == psx_disc || "$name" == psx_disc_lightrec ]]; then
        bios=${PSX_BIOS:-"$PROJECT_ROOT/../scph1001.bin"}
        if [[ ! -f "$bios" ]] || [[ $(stat -c %s "$bios") -ne 524288 ]]; then
            echo "PSX_BIOS must name a 524288-byte SCPH-1001 image" >&2
            exit 1
        fi
        bios_sha256=$(sha256sum "$bios" | awk '{print $1}')
        if [[ "$bios_sha256" != 71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3 ]]; then
            echo "PSX_BIOS is not the verified SCPH-1001 image" >&2
            exit 1
        fi
        cp "$bios" "$out/scph1001.bin"
        extra_sources=("$PROJECT_ROOT"/software/psx/*.c)
        program_cflags=(-O3 -flto -DPSX_GPU_ACCEL=1)
    fi
    if [[ "$name" == psx_bios_lightrec || "$name" == psx_disc_lightrec ]]; then
        lightrec="$out/lightrec"
        python3 "$PROJECT_ROOT/tools/lightrec_build.py" "$lightrec" >/dev/null
        extra_sources+=("$PROJECT_ROOT/software/lightrec/psx_lightrec.c"
            "$PROJECT_ROOT/software/lightrec/runtime.c"
            "$PROJECT_ROOT/software/lightrec/tpx_runtime.c")
        if [[ "$name" == psx_bios_lightrec ]]; then
            program_cflags+=(-DPSX_BIOS_LIGHTREC=1)
        else
            program_cflags+=(-DPSX_DISC_LIGHTREC=1)
        fi
        program_includes=(-I"$lightrec/include" -I"$lightrec/lightning/include"
            -I"$PROJECT_ROOT/software/lightrec"
            -I"$PROJECT_ROOT/third_party/lightrec")
        link_libraries=("$lightrec/liblightrec.a" -Wl,--start-group
            -lc_nano -lgcc -Wl,--end-group)
    fi
    if [[ "$name" == psx_perf ]]; then
        extra_sources=("$PROJECT_ROOT"/software/psx/*.c)
        program_cflags=(-O3 -flto)
    fi
    "$CC" "${CFLAGS[@]}" "${program_cflags[@]}" \
        -I"$PROJECT_ROOT/software/psx" -I"$out" "${program_includes[@]}" \
        "${LDFLAGS[@]}" -Wa,-I"$out" -o "$out/$name.elf" \
        "$COMMON/crt0.S" "$src"/*.c "${extra_sources[@]}" \
        $(ls "$src"/*.S 2>/dev/null) "${link_libraries[@]}"
    "$OBJCOPY" -O binary "$out/$name.elf" "$out/$name.bin"
    python3 "$PROJECT_ROOT/tools/ae350_run.py" pack "$out/$name.bin" -o "$out/$name.tpx"
done
