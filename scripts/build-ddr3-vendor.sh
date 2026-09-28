#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Build the standalone Gowin DDR3 controller test for the Tang Console 138K.
# The four Gowin placement strategies are run in parallel so their timing can
# be compared before an image is chosen for hardware.  Override the set with
# TANG_PSX_PLACE_OPTIONS="3" for a single build.

set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$PROJECT_ROOT/scripts/env.sh"

OUT_DIR="$PROJECT_ROOT/build/ddr3-vendor"
IP_DIR="$OUT_DIR/ip"
SRC_DIR="$PROJECT_ROOT/gateware/ddr3_vendor"
PHOSPHOR="$PROJECT_ROOT/third_party/tang-phosphor"
PLACE_OPTIONS=${TANG_PSX_PLACE_OPTIONS:-"1 2 3 4"}

"$PROJECT_ROOT/scripts/gen-ddr3-ip.sh" "$IP_DIR"

pids=()
for option in $PLACE_OPTIONS; do
    build="$OUT_DIR/place$option"
    rm -rf "$build"
    mkdir -p "$build"
    cat > "$build/run.tcl" <<EOF
set_device -name GW5AST-138C GW5AST-LV138PG484AC1/I0
add_file {$SRC_DIR/tang_psx_ddr3_top.sv}
add_file {$SRC_DIR/ddr3_tester.sv}
add_file {$SRC_DIR/status_snapshot.sv}
add_file {$IP_DIR/ddr3_memory_interface/ddr3_memory_interface.v}
add_file {$IP_DIR/gowin_pll/gowin_pll_mod.v}
add_file {$IP_DIR/gowin_pll/pll_init.v}
add_file {$PHOSPHOR/src/iosys/iosys_bl616.v}
add_file {$PHOSPHOR/src/iosys/uart_fixed.v}
add_file {$PHOSPHOR/src/iosys/textdisp.v}
add_file {$PHOSPHOR/src/iosys/gowin_dpb_menu.v}
add_file {$SRC_DIR/tang_psx_ddr3.cst}
add_file {$SRC_DIR/tang_psx_ddr3.sdc}
set_option -top_module tang_psx_ddr3_top
set_option -output_base_name tang_psx_ddr3
set_option -verilog_std sysv2017
set_option -use_ready_as_gpio 1
set_option -use_done_as_gpio 1
set_option -use_mspi_as_gpio 1
set_option -use_cpu_as_gpio 0
set_option -rw_check_on_ram 1
set_option -bit_security 0
set_option -bit_encrypt 0
set_option -bit_compress 0
set_option -multi_boot 1
set_option -place_option $option
run all
EOF
    (cd "$build" && gw_sh run.tcl > build.log 2>&1) &
    pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
    wait "$pid" || status=1
done

builds=()
for option in $PLACE_OPTIONS; do
    build="$OUT_DIR/place$option"
    bitstream="$build/impl/pnr/tang_psx_ddr3.bin"
    if [[ -s "$bitstream" ]]; then
        install -m 0644 "$bitstream" "$OUT_DIR/tang-psx-ddr3-place$option.bin"
        builds+=("$build")
    else
        echo "place option $option produced no bitstream; see $build/build.log" >&2
        status=1
    fi
done

if (( ${#builds[@]} )); then
    python3 "$PROJECT_ROOT/scripts/gowin-timing-summary.py" "${builds[@]}" || true
    (cd "$OUT_DIR" && sha256sum tang-psx-ddr3-place*.bin)
fi
exit $status
