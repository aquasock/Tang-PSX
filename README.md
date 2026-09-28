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
| `0x04` | ABI version (`0x00020000`) |
| `0x08` | Firmware stage; bit 31 means complete |
| `0x0c` | Failure code; zero means no detected failure |
| `0x10` | Number of DDR words verified |
| `0x14` | DDR test checksum |
| `0x18` | JIT probe results: first result in bits 31:16, second in 15:0 |
| `0x1c` | RV32 cycle count consumed by the Gate 1 checks |
| `0x20` | Passed-feature bitmap |
| `0x24` | Tang-Control transport CRC error count |
| `0x28` | Tang-Control malformed-request count |
| `0x2c` | Clock/reset status bitmap |
| `0x30` | Firmware log byte count |
| `0x34` | DDR3 calibration time in 75 MHz diagnostic-clock cycles |
| `0x40`-`0xbc` | Firmware log ring (32 words, little-endian text) |
| `0xc0` | Address of the first failed check |
| `0xc4` | Expected value of the first failed check |
| `0xc8` | Observed value of the first failed check |

Feature bits are: bit 0 DDR3 calibration, bit 1 fixed-pattern read/write,
bit 2 executable DDR plus `fence.i`, bit 3 walking address bits over 1 GiB,
bit 4 byte and halfword stores, and bit 5 interleaved read-after-write.

Clock/reset status bits are: bit 0 AE350 PLL locked, bit 1 system reset, bit 2
DDR3 memory PLL locked, bit 3 DDR3 calibration complete, bit 4 system PLL
locked, bit 5 software-requested CPU reset, bit 6 asserted external reset, and
bit 7 DDR3 read-return FIFO overflow. The diagnostic transport runs from the
AE350 PLL's independent 75 MHz output so these values remain readable while
the DDR3 clocks start.

The AE350 reaches the 1 GiB DDR3 at `0x40000000` through its direct RAM
bridge, a 75-to-100 MHz native-port crossing, and `gowin_ddr3_native`, which
adapts LiteDRAM's native port to the Gowin DDR3 controller described below.

## Build

Initialize dependencies once:

```sh
git submodule update --init
git submodule update --init --recursive third_party/pythondata-software-picolibc
```

Then build with:

```sh
scripts/build-gate1.sh    # place options 1-4 in parallel, with timing summary
```

The build expects Gowin EDA 1.9.11.03 and an RV32-capable
`riscv64-unknown-elf` toolchain. The local Tang development cache and Gowin
installation are detected automatically; `RISCV_TOOLCHAIN_BIN` and
`GOWIN_EDA_BIN` can override them.

The build regenerates the Gowin DDR3 IP into `build/gate1-ip` first. The
Tang-Control loadable images are `build/gate1-place<N>/tang-psx-gate1.bin`. The
larger `.fs` file beside the generated gateware is Gowin's textual fuse image,
not the SD-card core format.

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
