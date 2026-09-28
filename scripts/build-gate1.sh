#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$PROJECT_ROOT/scripts/env.sh"

cd "$PROJECT_ROOT"
python3 gateware/ae350_gate1.py --build \
    --place-option "${TANG_PSX_PLACE_OPTION:-3}" \
    --output-dir "$PROJECT_ROOT/build/gate1"

RAW_BITSTREAM="$PROJECT_ROOT/build/gate1/gateware/impl/pnr/project.bin"
CORE_IMAGE="$PROJECT_ROOT/build/gate1/tang-psx-gate1.bin"
if [[ ! -s "$RAW_BITSTREAM" ]]; then
    echo "Missing Gowin raw bitstream: $RAW_BITSTREAM" >&2
    exit 1
fi
install -m 0644 "$RAW_BITSTREAM" "$CORE_IMAGE"
sha256sum "$CORE_IMAGE"
