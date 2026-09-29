# Tang-PSX

<img width="1312" height="602" alt="tang-psx-bios-progress2" src="https://github.com/user-attachments/assets/dc74ee7b-dcc3-421a-aa08-dea1c7bbf11c" />

<img width="640" height="480" alt="spyro_expected_35s" src="https://github.com/user-attachments/assets/9c0b98ae-314c-4456-990b-ac1d37191b18" />


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
| `0x04` | ABI version (`0x00020002`) |
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
| `0x200` | Disc request sequence, advanced by firmware for each request |
| `0x204` | Disc request byte offset in the `.bin` image |
| `0x208` | Disc request byte length |
| `0x20c` | Disc size in 2352-byte sectors; written by Tang-Control, 0 = no disc |

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

The A25 core clock comes from the AE350's dedicated PLL at `PLL_R[0]`
(`gateware/ae350_pll.v`, 50 MHz x 15 = 750 MHz VCO). The macro takes its core
clock from that PLL's `CLKOUT1`, whatever the netlist connects to `CORE_CLK`:
with 750 MHz on `CLKOUT0` and 75 MHz on `CLKOUT1`, the core measured 75 MHz,
and it followed `CLKOUT1` to 50 MHz when only that divider changed. The CPU
clock is therefore generated on `CLKOUT1` (`ODIV1 = 1`, 750 MHz) and wired to
`CORE_CLK` from there. `build/programs/clock/clock.tpx` checks it: it runs a
dependent `addi` chain for 2^30 counted cycles, which `tools/ae350_run.py run`
reports finishing about 1.4 s after the stream starts at 750 MHz. Gate 1 images
built before this fix ran the core at 75 MHz.

## AE350 program loader

After the Gate 1 checks pass, the ROM firmware waits for Tang-Control stream
images. Each `.tpx` image is a 32-byte little-endian header followed by a flat
RV32 payload. The header supplies the DDR3 load address, entry address, payload
length, and IEEE/zlib CRC-32. The loader checks the header, address range,
stream length, and CRC, writes back and invalidates the D-cache, executes
`fence.i`, and calls the entry point with the API in
`software/common/tpx_api.h`. A returning program publishes its 32-bit result
at debug address `0xdc` and the loader waits for another image.

Loader API version 2 adds disc access. `disc_request(offset, length)` writes
the byte range to the mailbox at debug addresses `0x204`/`0x208` and then
advances the sequence at `0x200`. Tang-Control's `tangpsx_disc` service polls
the mailbox while Gate 1 is loaded and answers each new sequence with one stream
session of that range, which `stream_read` returns entry by entry without
blocking. `disc_sectors` reports the size of the disc Tang-Control publishes:
the `.bin` named by the first `.cue` file in the SD-card root.

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
python3 tools/ae350_run.py run psx_bios.tpx --timeout 60
```

The run progresses from black through a brightening gray background, animates
the orange/red diamond, and finishes with the blue `SONY`, `TM`, and
`COMPUTER ENTERTAINMENT` text, about 5.9 s after start on the 750 MHz AE350.
Success returns `0xb1051001` at stage `0x80011001`. The event-driven checkpoint
counts 27,870,497 guest instructions (21,984,983 of them in signature-checked
loop accelerators), consumes 10,768 GPU words and 158,497 DMA words, draws 414
primitives, performs 63 image uploads, and delivers 144 VBlanks. The runner
gives up after 30 s with result `0xdead1002`, publishes progress registers once
per second, and ends its log with a timing summary such as
`ms t5866 v13 1510 cpu 5583 gpu 3753 acc 127 dsp 125` (total, time to VBlank 13,
CPU emulation including GPU, GPU, accelerators, and display copy, in
milliseconds) followed by `jit <compiled> fb <interpreted> fl <flushes>`
instruction and cache-flush counts. This proves the startup-logo path; CD
media, controllers, memory cards, audio synthesis, and continued execution
into the BIOS menu or a game remain future work.

The R3000A runs through a hybrid RV32 recompiler (`software/psx/jit.c`) that
compiles hot basic blocks and interprets everything else up to the end of the
current basic block. Triangles are rasterized with incrementally stepped edge
functions and exact quotient/remainder interpolation, so each pixel needs no
division yet matches the original per-pixel barycentric division bit for bit.
Three host checks cover this:

```sh
python3 tests/test_psx_gpu.py       # 40,000 random polygons vs the pinned reference rasterizer
python3 tests/test_psx_jit.py       # generated RV32 code under qemu-riscv32
PSX_BIOS=/path/to/scph1001.bin python3 tests/test_psx_bios_jit.py
```

The last boots the BIOS to the logo through the real RV32 JIT under
`qemu-riscv32` and requires the host checkpoint's telemetry and framebuffer
exactly. Because the service loop is event driven, its JIT statistics are the
ones the AE350 produces.

An opt-in `psx_bios_lightrec.tpx` runs the same logo checkpoint with Lightrec
instead of the hybrid JIT, retaining the fabric GPU and the existing JIT image
for comparison. Build it with the verified BIOS and patched RV32 toolchain:

```sh
PSX_BIOS=/path/to/scph1001.bin scripts/build-programs.sh psx_bios_lightrec
PSX_BIOS=/path/to/scph1001.bin python3 tests/test_psx_bios_lightrec.py
python3 tools/ae350_run.py upload build/programs/psx_bios_lightrec/psx_bios_lightrec.tpx
# Load Gate 1 before running this command.
python3 tools/ae350_run.py run psx_bios_lightrec.tpx --timeout 150
```

The Lightrec image reports the same logo telemetry in the loader registers,
with CPU and GPU time in the profile registers and compiled block count, code
bytes, and heap usage in the log. A failed initialization or unexpected
Lightrec exit publishes a nonzero failure and a distinct `0x8001bad*` stage.
Its 120 s device-side timeout is separate from the unchanged JIT image's 30 s
timeout. QEMU instruction counts are not a hardware speed measurement.

## Booting a disc

`psx_disc.tpx` boots the BIOS with the disc image on the SD card. Put a
`.cue`/`.bin` pair (one 2352-byte data track) in the card's root, load Gate 1,
and run it:

```sh
PSX_BIOS=/path/to/scph1001.bin scripts/build-programs.sh psx_disc
python3 tools/ae350_run.py upload build/programs/psx_disc/psx_disc.tpx
# Load Gate 1 before running this command.
python3 tools/ae350_run.py run psx_disc.tpx --reset --detach --timeout 120
```

The program requests 32-sector windows through the loader's disc API and
reads ahead one window. VBlank runs at 60 Hz of emulated time, and while the
CPU only waits for it the machine clock skips ahead to the next frame, so
device latency seen by software matches hardware. When Gate 1's display
blitter is present, HDMI is refreshed every VBlank; otherwise the CPU copies
the display every third frame. It runs until the core is reset and publishes VBlanks, sectors, requests,
retries, and cache misses once per second; Tang-Control's `status` lists the
disc it serves. The runner's `--detach` option returns after the image starts,
leaving the emulator active.

To compare Lightrec on the same disc and fabric GPU, build a separate
`psx_disc_lightrec.tpx` image; this does not replace the JIT-based image:

```sh
PSX_BIOS=/path/to/scph1001.bin scripts/build-programs.sh psx_disc_lightrec
python3 tools/ae350_run.py upload build/programs/psx_disc_lightrec/psx_disc_lightrec.tpx
# Load Gate 1 and publish the disc before running this command.
python3 tools/ae350_run.py run psx_disc_lightrec.tpx --detach --timeout 120
python3 tools/ae350_run.py status
```

Its status includes cumulative CPU, GPU, and display milliseconds and VBlank
count. Initialization or unexpected Lightrec exits publish a nonzero failure
and a distinct `0x8002bad*` stage. Measure VBlank progress over the same game
scene when comparing core performance.

The local QEMU regression compares the JIT and Lightrec through the Spyro
handoff, and can check Lightrec's later menu-range progress (no disc data is
copied into the repository):

```sh
PSX_BIOS=/path/to/scph1001.bin PSX_DISC=/path/to/spyro.bin python3 tests/test_psx_disc_lightrec.py
PSX_BIOS=/path/to/scph1001.bin PSX_DISC=/path/to/spyro.bin PSX_DISC_VBLANKS=2700 python3 tests/test_psx_disc_lightrec.py
```

The machine adds a CD-ROM controller (`software/psx/cdrom.c`: commands,
interrupt handshake, 1x/2x sector timing, 2048/2340-byte delivery, DMA channel
3), the controller port with a digital pad in slot 1 (`software/psx/sio.c`),
and the GTE lighting and color commands. With Spyro the Dragon (USA) the BIOS
passes its license check, shows the PlayStation logo, and boots
`SCUS_942.28`. PsyQ LibCD's two-stage raw-sector transfer is supported: its
12-byte header read followed by a repeated FIFO-enable write preserves the
FIFO cursor, allowing the subsequent 2048-byte DMA to receive user data.

`tests/psx_disc_host.c` runs the same boot on the host from a local `.bin`
(`psx_disc_host BIOS BIN SECONDS [PPM]`), with diagnostics listed in its
header: CD traces, kernel console output, call traces, RAM dumps for
`tools/mipsdis.py`, and a RAM watchpoint.

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

The display blitter at `0xea000000` performs that conversion in fabric. It reads
PSX VRAM at `0x7fe00000` one source row at a time through its own DDR3 port,
scales nearest-neighbour to 640x480, converts BGR555 to RGB565, and writes the
framebuffer. Its registers are magic `0x44535031` (`DSP1`) at offset `0x00`;
status at `0x04` (bit 0 ready, bit 1 busy, bit 2 error; writing 1 to bit 0
starts a blit); origin `x | y << 16` at `0x08`, wrapped within VRAM; size
`w | h << 16` at `0x0c`, at most 640x480; completed frames at `0x10`; and busy
cycles at `0x14`. `software/common/psx_display.h` detects the blitter by its
magic and blocks until the blit finishes; the BIOS and disc programs fall back
to the CPU copy when it is absent.

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
not the SD-card core format. Diagnostic variants pass extra
`gateware/ae350_gate1.py` options in `TANG_PSX_GATE1_ARGS` (for example
`--ae350-cpu-odiv 2`) and build under `build/<TANG_PSX_GATE1_NAME>-place<N>`.

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
