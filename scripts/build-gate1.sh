#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$PROJECT_ROOT/scripts/env.sh"

# The four Gowin placement strategies are built in parallel so their timing can
# be compared before an image is chosen for hardware.  Override the set with
# TANG_PSX_PLACE_OPTIONS="3" for a single build.  Diagnostic variants pass
# extra ae350_gate1.py options in TANG_PSX_GATE1_ARGS and build under
# build/<TANG_PSX_GATE1_NAME>-place<N> so the default builds are kept.
PLACE_OPTIONS=${TANG_PSX_PLACE_OPTIONS:-"1 2 3 4"}
GATE1_NAME=${TANG_PSX_GATE1_NAME:-gate1}
read -r -a GATE1_ARGS <<< "${TANG_PSX_GATE1_ARGS:-}"
IP_DIR="$PROJECT_ROOT/build/gate1-ip"

"$PROJECT_ROOT/scripts/gen-ddr3-ip.sh" "$IP_DIR"

cd "$PROJECT_ROOT"
pids=()
for option in $PLACE_OPTIONS; do
    out="$PROJECT_ROOT/build/$GATE1_NAME-place$option"
    rm -rf "$out"
    mkdir -p "$out"
    python3 gateware/ae350_gate1.py --build \
        --place-option "$option" \
        --ip-dir "$IP_DIR" \
        --output-dir "$out" "${GATE1_ARGS[@]}" > "$out/build.log" 2>&1 &
    pids+=("$!")
done

status=0
for pid in "${pids[@]}"; do
    wait "$pid" || status=1
done

builds=()
for option in $PLACE_OPTIONS; do
    out="$PROJECT_ROOT/build/$GATE1_NAME-place$option"
    raw="$out/gateware/impl/pnr/project.bin"
    if [[ -s "$raw" ]]; then
        # The larger .fs beside it is Gowin's textual fuse image, not the
        # Tang-Control core format.
        install -m 0644 "$raw" "$out/tang-psx-gate1.bin"
        builds+=("$out/gateware")
    else
        echo "place option $option produced no bitstream; see $out/build.log" >&2
        status=1
    fi
done

if (( ${#builds[@]} )); then
    python3 "$PROJECT_ROOT/scripts/gowin-timing-summary.py" "${builds[@]}" || true
    for option in $PLACE_OPTIONS; do
        image="build/$GATE1_NAME-place$option/tang-psx-gate1.bin"
        [[ -s "$image" ]] && sha256sum "$image"
    done
fi
exit $status
