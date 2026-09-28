# Tang-PSX

Tang-PSX is an experimental PlayStation core for the Sipeed Tang Console
138K. It combines the GW5AST AE350 hard RV32 processor with FPGA peripherals
and uses the Tang-Control BL616 firmware and transport.

The first engineering gate is deliberately headless. It must prove that the
AE350 can initialize and use board DDR3, execute newly generated RV32 code,
and expose deterministic results through Tang-Control before PSX hardware is
added.

## Gate 1 status ABI

The diagnostic image identifies as core `0x51`. Tang-Control `peek` reads:

| Address | Meaning |
| --- | --- |
| `0x00` | Magic `0x54505831` (`TPX1`) |
| `0x04` | ABI version (`0x00010000`) |
| `0x08` | Firmware stage; bit 31 means complete |
| `0x0c` | Failure code; zero means no detected failure |
| `0x10` | Number of DDR words verified |
| `0x14` | DDR test checksum |
| `0x18` | JIT probe results: first result in bits 31:16, second in 15:0 |
| `0x1c` | RV32 cycle count consumed by the DDR and JIT probes |
| `0x20` | Passed-feature bitmap |
| `0x24` | Tang-Control transport CRC error count |
| `0x28` | Tang-Control malformed-request count |
| `0x2c` | Clock/reset status bitmap |

Feature bit 0 is DDR initialization, bit 1 is DDR read/write verification,
and bit 2 is executable DDR plus `fence.i` coherency.

Clock/reset status bits are: bit 0 AE350 PLL locked, bit 1 system reset, bit 2
DDR fast-clock stop, bit 3 DDR clock-divider reset, bit 4 system PLL locked,
bit 5 software-requested CPU reset, and bit 6 asserted external reset. The
diagnostic transport runs from the AE350 PLL's independent 75 MHz output so
these values remain readable while the DDR clock sequence pauses the system
domain.

## Build

Initialize dependencies once:

```sh
git submodule update --init
git submodule update --init --recursive third_party/pythondata-software-picolibc
```

Then build with:

```sh
scripts/build-gate1.sh
```

The build expects Gowin EDA 1.9.11.03 and an RV32-capable
`riscv64-unknown-elf` toolchain. The local Tang development cache and Gowin
installation are detected automatically; `RISCV_TOOLCHAIN_BIN` and
`GOWIN_EDA_BIN` can override them.

The Tang-Control loadable image is `build/gate1/tang-psx-gate1.bin`. The
larger `.fs` file beside the generated gateware is Gowin's textual fuse image,
not the SD-card core format.
# Tang-PSX

## Standalone DDR3 controller test

`gateware/ddr3_vendor` proves the full x32 DDR3 array with Gowin's DDR3
controller (400 MHz memory clock, 1:4 ratio, 256-bit native port) before it is
bridged to the AE350. Its controller, PLL, and PG484 pin configuration follow
Sipeed's TangMega-138K-example `ddr_memory` design.

Gowin's generated IP is licensed with Gowin EDA and is not committed.
`scripts/gen-ddr3-ip.sh` regenerates it from the committed configuration into
`build/ddr3-vendor/ip` and checks the emitted `.ipc` against
`gateware/ddr3_vendor/ddr3_memory_interface.ipc`.

```sh
scripts/build-ddr3-vendor.sh      # place options 1-4 in parallel, with timing summary
python3 tools/ddr3_vendor_status.py [--watch SECONDS]
```

Images are written to `build/ddr3-vendor/tang-psx-ddr3-place<N>.bin`. The test
writes and verifies the whole 1 GiB continuously, complementing the pattern on
alternate passes; its register map is in `tang_psx_ddr3_top.sv`.
