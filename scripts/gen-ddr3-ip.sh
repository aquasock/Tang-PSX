#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Regenerate the Gowin DDR3 controller and its PLL from the committed
# configuration in gateware/ddr3_vendor.  Gowin's generated (encrypted) RTL is
# licensed with Gowin EDA and is never committed; it is produced here from the
# local installation into build/ddr3-vendor/ip.

set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$PROJECT_ROOT/scripts/env.sh"

CONFIG_DIR="$PROJECT_ROOT/gateware/ddr3_vendor"
IP_DIR="${1:-$PROJECT_ROOT/build/ddr3-vendor/ip}"

rm -rf "$IP_DIR"
mkdir -p "$IP_DIR/project" "$IP_DIR/gowin_pll"

# gw_sh's read_ipc crashes in batch mode, so the IP is created and configured
# through set_property instead; the emitted .ipc proves the configuration.
cat > "$IP_DIR/gen.tcl" <<EOF
create_project -name ddr3_ip -dir {$IP_DIR/project} -pn GW5AST-LV138PG484AC1/I0 -device_version C -force
create_ipc -name ddr3 -dir {$IP_DIR} -module_name DDR3_Memory_Interface_Top -file_name ddr3_memory_interface -language Verilog
source {$CONFIG_DIR/ddr3_ip.tcl}
generate_target [get_ips DDR3_Memory_Interface_Top]
EOF
(cd "$IP_DIR" && gw_sh gen.tcl > gen.log 2>&1) || {
    tail -20 "$IP_DIR/gen.log" >&2
    exit 1
}

DDR3_RTL="$IP_DIR/ddr3_memory_interface/ddr3_memory_interface.v"
if [[ ! -s "$DDR3_RTL" ]]; then
    echo "DDR3 IP generation produced no RTL; see $IP_DIR/gen.log" >&2
    exit 1
fi
if ! diff -u "$CONFIG_DIR/ddr3_memory_interface.ipc" \
        "$IP_DIR/ddr3_memory_interface/ddr3_memory_interface.ipc"; then
    echo "Generated DDR3 .ipc differs from the committed reference" >&2
    exit 1
fi

sed "s#@OUTPUT_DIR@#$IP_DIR/gowin_pll#" "$CONFIG_DIR/gowin_pll.mod" \
    > "$IP_DIR/gowin_pll/gowin_pll.mod"
(cd "$IP_DIR/gowin_pll" && GowinModGen -do gowin_pll.mod > gen.log 2>&1) || {
    cat "$IP_DIR/gowin_pll/gen.log" >&2
    exit 1
}
if [[ ! -s "$IP_DIR/gowin_pll/gowin_pll_mod.v" ]]; then
    echo "PLL generation produced no RTL; see $IP_DIR/gowin_pll/gen.log" >&2
    exit 1
fi

# PLL_INIT is distributed with the Gowin PLL_ADV IP core.
PLL_INIT="$GOWIN_EDA_BIN/../ipcore/PLL_ADV/data/PLL/pll_init.v"
install -m 0644 "$PLL_INIT" "$IP_DIR/gowin_pll/pll_init.v"

echo "Generated Gowin DDR3 and PLL IP in $IP_DIR"
