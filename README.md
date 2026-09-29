# Tang-PSX

Tang-PSX is an experimental PlayStation core for the Sipeed Tang Console
138K. It combines the GW5AST AE350 hard RV32 processor with FPGA peripherals
and uses the Tang-Control BL616 firmware and transport.

The first engineering gate proved that the AE350 can initialize and use board
DDR3, execute newly generated RV32 code, and expose deterministic results
through Tang-Control. The current image also drives HDMI from a software-filled
DDR3 framebuffer as the first visible memory-backed milestone before PSX
hardware is added.

## Gate 1 status ABI

The diagnostic image identifies as core `0x51`. Tang-Control `peek` reads:

| Address | Meaning |
| --- | --- |
| `0x00` | Magic `0x54505831` (`TPX1`) |
| `0x04` | ABI version (`0x00020001`) |
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
| `0x34` | DDR3 calibration time in 50 MHz diagnostic-clock cycles |
| `0x40`-`0xbc` | Firmware log ring (32 words, little-endian text) |
| `0xc0` | Address of the first failed check |
| `0xc4` | Expected value of the first failed check |
| `0xc8` | Observed value of the first failed check |
| `0xd0` | Loader state in bits 7:0 and completed-run count in bits 31:16 |
| `0xd4` | Payload size of the last accepted image |
| `0xd8` | Computed CRC-32 of the last image payload |
| `0xdc` | Return value of the last loaded program |
| `0xe0` | Tang-Control stream session count |
| `0xe4` | Tang-Control stream byte count |
| `0xe8` | Tang-Control stream end count |
| `0xec` | Tang-Control stream cancel count |
| `0xf0` | Sticky stream-event overflow flag |
| `0xf4` | Video status: bit 0 DMA enabled, bit 1 sticky scanout underflow |

Writing bit 0 to debug address `0x100` resets the AE350 for 31 system-clock
cycles. The FPGA stream receiver and its counters remain active across this
CPU reset.

Feature bits are: bit 0 DDR3 calibration, bit 1 fixed-pattern read/write with
caches off, bit 2 executable DDR plus `fence.i`, bit 3 walking address bits
over 1 GiB, bit 4 byte and halfword stores, bit 5 interleaved read-after-write,
bit 6 AE350 instruction and data caches enabled, and bit 7 the DDR-backed
framebuffer drawn and enabled. Checks after bit 6 run with both caches on and
write back and invalidate the D-cache before each verify, so verified data is
read from DDR3. The firmware log reports the cache configuration CSRs, the
uncached and cached 1 MiB write/read cycle counts, and the value returned by
rewritten code before `fence.i`.

Clock/reset status bits are: bit 0 AE350 PLL locked, bit 1 system reset, bit 2
DDR3 memory PLL locked, bit 3 DDR3 calibration complete, bit 4 system PLL
locked, bit 5 software-requested CPU reset, bit 6 asserted external reset, and
bit 7 DDR3 read-return FIFO overflow. The diagnostic transport runs from the
50 MHz board clock, independent of the system and DDR3 clocks, so these values
remain readable while the DDR3 clocks start.

The AE350 reaches the 1 GiB DDR3 at `0x40000000` through `Gate1RAMBridge`
(`gateware/ae350_ram_bridge.py`), a 75-to-100 MHz native-port crossing, and
`gowin_ddr3_native`, which adapts LiteDRAM's native port to the Gowin DDR3
controller described below. `Gate1RAMBridge` registers the DDR3 Wishbone path
and merges cache-line bursts without address compares, so the 75 MHz system
clock closes timing; `python3 gateware/sim/test_ae350_ram_bridge.py` simulates
it.

## AE350 program loader

After the Gate 1 checks pass, the ROM firmware waits for Tang-Control stream
images. Each `.tpx` image is a 32-byte little-endian header followed by a flat
RV32 payload. The header supplies the DDR3 load address, entry address, payload
length, and IEEE/zlib CRC-32. The loader checks the header, address range,
stream length, and CRC, writes back and invalidates the D-cache, executes
`fence.i`, and calls the entry point with the API in
`software/common/tpx_api.h`. A returning program publishes its 32-bit result
at debug address `0xdc` and the loader waits for another image.

Build the example programs and simulate the clock-domain-crossing receiver:

```sh
scripts/build-programs.sh
python3 gateware/sim/test_stream_loader.py
```

Tang-Control only accepts SD-card uploads while its main menu is active. Upload
the generated images before loading Gate 1, then stream them while Gate 1 is
running:

```sh
python3 tools/ae350_run.py upload build/programs/hello/hello.tpx
python3 tools/ae350_run.py upload build/programs/blob/blob.tpx
python3 tools/ae350_run.py run hello.tpx
python3 tools/ae350_run.py run blob.tpx
python3 tools/ae350_run.py run hello.tpx --reset
```

The last command resets the AE350, waits for the ROM checks and loader to become
ready again, then streams the program. `python3 tools/ae350_run.py status`
prints the loader, stream, result-register, and firmware-log state.

## R3000A and software GTE diagnostic

`psx_diag.tpx` is the first PlayStation execution component running bare-metal
on the 750 MHz AE350. It contains a portable MIPS I interpreter with R3000A
load-delay, branch-delay, exception EPC/BD, COP0, and COP2 behavior, plus the
initial software GTE coordinate path. The GTE subset implements register
transfers, MVMVA, RTPS, RTPT, NCLIP, AVSZ3, and AVSZ4; command latency, the
lighting/color commands, and saturation edge cases outside the generated
vectors remain future work and this diagnostic is not yet a BIOS-capable
machine emulator.

The PC-side generator emits nine CPU and six GTE cases. The native regression
checks 96 deterministic results and deliberately mutates ADDU to prove that
the vectors detect a bad interpreter. Build and test with:

```sh
python3 tests/test_psx_diag.py
scripts/build-programs.sh psx_diag
```

Upload `build/programs/psx_diag/psx_diag.tpx` from the TangCore main menu, load
the Gate 1 image, then run `python3 tools/ae350_run.py run psx_diag.tpx`. A pass
returns `0x3000a001`, publishes stage `0x80003001`, 96 passed checks, vector
state checksum `0x9349f3af`, and changes HDMI to a green `PSX CPU / GTE PASS`
card. The generated vector set itself has CRC-32 `0xc2527637`. A
failure instead draws a red `GTE FAIL` card and puts the vector/check selector,
expected value, and observed value in the standard failure registers.

## SCPH-1001 BIOS logo checkpoint

`psx_bios.tpx` runs the North American SCPH-1001 ROM through the portable
R3000A machine on the 750 MHz AE350. The machine supplies 2 MiB mirrored main
RAM, scratchpad and BIOS mappings, interrupt/VBlank state, GPU and ordering-table
DMA, essential timer/CD-ROM/SPU register behavior, and a software GPU backed by
1024x512 BGR555 VRAM. GP0 drawing and transfer commands render the real BIOS
startup command stream, which is resampled into Gate 1's 640x480 RGB565 HDMI
framebuffer while execution continues.

The copyrighted BIOS is never stored in this repository. By default the build
expects the verified 512 KiB `scph1001.bin` beside the repository, or accepts
an explicit path through `PSX_BIOS`. Its required SHA-256 is
`71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3`.

```sh
PSX_BIOS=/path/to/scph1001.bin scripts/build-programs.sh psx_bios
PSX_BIOS=/path/to/scph1001.bin python3 tests/test_psx_bios.py
python3 tools/ae350_run.py upload build/programs/psx_bios/psx_bios.tpx
# Load Gate 1 before running this command.
python3 tools/ae350_run.py run psx_bios.tpx --timeout 900
```

The run progresses from black through a brightening gray background, animates
the orange/red diamond, and finishes with the blue `SONY`, `TM`, and
`COMPUTER ENTERTAINMENT` text. Success returns `0xb1051001` at stage
`0x80011001`. The deterministic checkpoint executes 99,999,544 guest
instructions, consumes 10,768 GPU words and 158,497 DMA words, draws 414
primitives, and performs 63 image uploads. This proves the startup-logo path;
CD media, controllers, memory cards, audio synthesis, and continued execution
into the BIOS menu or a game remain future work.

## HDMI framebuffer output

Gate 1 scans a contiguous 640x480 RGB565 framebuffer from `0x7ff00000` in
DDR3. Its 614,400 bytes are separate from the normal test and program-loading
areas. A fair native-port arbiter shares the 256-bit DDR3 controller between
AE350 reads/writes and the read-only video DMA, whose 4 KiB FIFO holds more
than three active lines. ROM firmware draws a bordered gradient/checker image,
writes back the data cache, and enables scanout; debug address `0xf4` reports
whether DMA started and whether it ever starved.

Loaded programs can draw through the `TPX_FRAMEBUFFER_*` constants in
`software/common/tpx_api.h`; call the API's `flush_dcache` function after
updates so the continuously looping DMA sees the new pixels.

The system PLL supplies a 125 MHz HDMI serializer clock, divided by five for a
25 MHz pixel clock. Standard 640x480 blanking produces a 59.52 Hz refresh rate.
The BIOS program converts the active display region from its separate
1024x512 BGR555 PlayStation VRAM into this scanout surface.

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
