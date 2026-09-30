# TANG-PSX TECHNICAL REFERENCE

> **Project:** Tang-PSX
> **Purpose:** The authoritative lookup for externally defined facts this project depends on: device and board specifications, memory standards and vendor IP behavior, processor architecture, vendor tool behavior, and the interface contracts of external projects.
> **Authority:** Per `.ai/core.md`, consult this file before looking anything up online. A record is authoritative to the extent of its `status` and cited `sources`. When a better or newer source is found online, add or supersede a record and notify the user.

This file records what an outside source (a standard, datasheet, schematic, vendor tool, or external repository) says, plus hardware observations that confirm or refute it. It does not record project history, build results, or discussion; those belong in `core-log.md`. It does not record project design choices unless an external fact forces them.

---

## 1. AI operating rules

```yaml
schema_version: 1

record_kinds:
  - DEVICE      # FPGA silicon: part, revision, resources, primitives
  - BOARD       # Tang Console 138K board: pins, parts, clocks, wiring
  - MEMORY      # memory standards and memory-controller IP behavior
  - PROCESSOR   # processor architecture: ISA, CSRs, caches, address map
  - TOOLCHAIN   # Gowin EDA, LiteX/Migen, compiler behavior
  - EXTERNAL    # interface contracts owned by other projects (Tang-Control, Tang-Phosphor)

statuses:
  VERIFIED:   "Primary source cited AND confirmed on this project's hardware or tools."
  SOURCED:    "Primary source cited; not yet confirmed on this project's hardware or tools."
  INFERRED:   "Derived from secondary sources, reverse inspection, or consistent observation; no primary source. Flag before relying on it."
  SUPERSEDED: "Replaced by the record named in superseded_by."

rules:
  - "One fact or one tightly coupled fact group per record."
  - "Every record cites at least one source with enough detail to re-check it: URL or repository path plus commit, document revision, file SHA-256, or tool version."
  - "Separate what the source says (statement) from what it means for this project (consequence)."
  - "Diagnostic or implementation limits chosen by this project are never recorded as standard or device limits."
  - "Do not rewrite a settled record's statement. Correct it by adding a new record and marking the old one SUPERSEDED with superseded_by."
  - "An INFERRED record must say how it was inferred, in verification."
  - "When an online lookup was needed because this file had no answer, add the finding here with the existing syntax, then notify the user if the source is newer or more valid than a record already here."
  - "Use technical names (AE350, BL616, DDR3). The user's conversational names are not used in this file."
```

---

## 2. Topic catalog

Topic IDs are the `record_id` prefix. An entry reserves a name; it does not claim coverage.

```yaml
- topic_id: DEV
  name: "GW5AST-138 FPGA device"
  description: "GW5AST-LV138PG484AC1/I0 silicon: revision, resources, primitives."

- topic_id: BRD
  name: "Tang Console 138K board"
  description: "Board wiring, fitted parts, clocks, and FPGA pin assignments."

- topic_id: DDR3
  name: "DDR3 memory and Gowin DDR3 controller IP"
  description: "The on-board x32 DDR3 array and the Gowin DDR3 Memory Interface IP that drives it."

- topic_id: SDR
  name: "SDR SDRAM add-on module"
  description: "The optional W9825G6KH-6 SDRAM module on the dock SDRAM connectors."

- topic_id: AE350
  name: "AE350 hard RISC-V processor"
  description: "The Andes-based AE350 subsystem inside the GW5AST: ISA, reset, address map, caches."

- topic_id: PSXCPU
  name: "PlayStation R3000A-compatible processor"
  description: "The PlayStation CPU's MIPS I instruction, pipeline-delay, exception, and coprocessor behavior."

- topic_id: GTE
  name: "PlayStation Geometry Transformation Engine"
  description: "COP2 register behavior, command encoding, fixed-point calculations, saturation, and pipeline timing."

- topic_id: PSXGPU
  name: "PlayStation GPU"
  description: "GPU I/O, commands, status, drawing, transfers, display state, and VRAM layout."

- topic_id: PSXIO
  name: "PlayStation memory map and peripherals"
  description: "Main RAM, scratchpad, BIOS, interrupts, DMA, timers, CD-ROM, and SPU register behavior."

- topic_id: TOOL
  name: "Toolchain behavior"
  description: "Gowin EDA and LiteX/Migen behavior that affects correctness or reproducibility."

- topic_id: TCTL
  name: "Tang-Control / BL616 transport"
  description: "The BL616 firmware's host commands and the FPGA-side iosys_bl616 debug transport."
```

---

## 3. Active routing

| Question | Consult first | Fast records |
|---|---|---|
| Which silicon revision is the board, and how is it identified? | DEV | DEV-001 |
| How much block RAM / logic does the device have? | DEV | DEV-002 |
| How wide is the DDR3 bus and what parts are fitted? | BRD | BRD-001 |
| Which FPGA pins carry DDR3, the clock, and the BL616 UART? | BRD | BRD-002, BRD-003 |
| Which FPGA pins reach the dock's PMOD sockets, and how are their pins numbered? | BRD | BRD-004 |
| Which PmodVGA pins carry colour and sync? | BRD | BRD-005 |
| What is on the FPGA module's 8-pin JTAG + UART connector? | BRD | BRD-006 |
| What DDR3 controller configuration is proven on this board? | DDR3 | DDR3-001 |
| How do I drive the Gowin DDR3 native (user) port correctly? | DDR3 | DDR3-002 |
| How is the native-port address laid out? | DDR3 | DDR3-003 |
| Which Gowin DDR3 IP version does our toolchain ship, and what differs? | DDR3 | DDR3-004 |
| What read latency does the Gowin controller show in Gate 1? | DDR3 | DDR3-005 |
| What is on the SDRAM add-on and which connector pins does it use? | SDR | SDR-001 |
| Where does the AE350 reset, and where are its bus windows? | AE350 | AE350-001 |
| How do I enable, inspect, and flush the AE350 caches? | AE350 | AE350-002 |
| Which AE350 address ranges are cached? Is the data cache write-back? | AE350 | AE350-003 |
| What cache geometry does this board's AE350 report? Does fence.i cover the D-cache? | AE350 | AE350-004 |
| Why do uncached AE350 benchmarks not measure memory speed? | AE350 | AE350-005 |
| How much of a system-clock cycle do the AE350 macro's RAM-port inputs need? | AE350 | AE350-006 |
| Which PLL output clocks the AE350 core? | AE350 | AE350-007 |
| Does this board's A25 have hardware cache prefetch? | AE350 | AE350-008 |
| What does an AE350 cache miss to DDR3 cost? | AE350 | AE350-009 |
| What does a hit in the fabric L2 cost the AE350? | AE350 | AE350-010 |
| Which CPU semantics must the R3000A interpreter preserve? | PSXCPU | PSXCPU-001 |
| Where are the PlayStation GTE registers and coordinate-command formulas documented? | GTE | GTE-001 |
| How are PlayStation GPU commands, status, transfers, drawing, and VRAM laid out? | PSXGPU | PSXGPU-001 |
| Which PlayStation address ranges, interrupts, and DMA channels are needed for BIOS startup? | PSXIO | PSXIO-001 |
| How does the PlayStation CD-ROM controller behave (registers, commands, interrupts, sector sizes)? | PSXIO | PSXIO-002 |
| When does DMA raise its interrupt? | PSXIO | PSXIO-003 |
| How does the controller port talk to a digital pad? | PSXIO | PSXIO-004 |
| How do the GTE lighting and color commands calculate? | GTE | GTE-002 |
| What limits 75 MHz timing in LiteDRAM's Wishbone burst frontend? | TOOL | TOOL-006 |
| How do I regenerate Gowin IP without the GUI? | TOOL | TOOL-001, TOOL-002 |
| Which Gowin place-and-route options can change timing closure? | TOOL | TOOL-007 |
| Can GNU Lightning / Lightrec generate RV32 code? | TOOL | TOOL-008 |
| How does ilp32d pass doubles in integer registers and on the stack? | TOOL | TOOL-009 |
| What does Lightrec expect of its host for interrupts, the GTE, cache isolation, and cycles? | TOOL | TOOL-010 |
| Why can git apply succeed without changing any file? | TOOL | TOOL-011 |
| Which GNU Lightning RISC-V backend defects break Lightrec, and is there a newer upstream RV32 port? | TOOL | TOOL-012 |
| Why does a Gowin SDC clock fail to attach to a net? | TOOL | TOOL-003 |
| Why did CSR timing change after removing LiteDRAM? | TOOL | TOOL-004 |
| How must a LiteX CSRStatus with fields be driven? | TOOL | TOOL-005 |
| How do I upload a core and read FPGA registers through Tang-Control? | TCTL | TCTL-001, TCTL-002 |
| How do I tell from tangctl status that a core is loaded? | TCTL | TCTL-003 |
| How does Tang-Control serve disc sectors to Tang-PSX? | TCTL | TCTL-004 |

---

## 4. Fast lookup index

```yaml
DEV-001: "Revision is the 5th character of the package's second marking line; this board (2518CA0N) is revision C"
DEV-002: "GW5AST-138: 138,240 LUTs, 340 BSRAM blocks of 18 Kbit"
BRD-001: "Two Hynix H5TQ4G63EFR-RDC x16 DDR3 devices form a 32-bit bus: DQ[31:0], DQS[3:0], DM[3:0], 1 GiB"
BRD-002: "x32 DDR3 pin map for PG484, SSTL15 at 1.5 V, from Sipeed's constraints"
BRD-003: "50 MHz oscillator on V22; BL616 UART to FPGA on V14 (FPGA RX) and U15 (FPGA TX), LVCMOS33"
BRD-004: "PMOD1 (beside HDMI) IO0-7 = W19 W20 F19 F20 E22 D22 E21 D21; PMOD0 IO0-7 = V18 V19 G21 G22 F18 E18 C22 B22; IO 2k is pin k+1, IO 2k+1 is pin k+7; LVCMOS33"
BRD-005: "Digilent PmodVGA: J1 pins 1-4 R0-R3, 7-10 B0-B3; J2 pins 1-4 G0-G3, 7 HS, 8 VS; bit 3 is the MSB; 3.3 V buffers"
BRD-006: "Module connector U1201 (JST SH 8-pin): 1 5V0 via diode, 2 TMS T13, 3 TDO U13, 4 TCK V12, 5 TDI R13, 6 RX V14, 7 TX U15, 8 GND; JTAG shared with the dock BL616"
DDR3-001: "Proven controller: 400 MHz memory clock, 1:4, CL6/CWL5, RTT_NOM 40, 256-bit native port on a 100 MHz user clock"
DDR3-002: "Native port: cmd 000 write / 001 read; command and its write data in the same cycle; wr_data_mask 1 = byte skipped; read data cannot be back-pressured"
DDR3-003: "addr[28:0] is in 32-bit words, one BL8 burst = 8 words; addr[28] (rank) is unused; addr[27:0] spans 1 GiB"
DDR3-004: "Gowin EDA 1.9.11.03 ships DDR3 IP 5.9; Sipeed's 6.0 adds AXI/arbitration/MC-BSRAM options; WRITE_RECOVERY is ignored at this configuration"
DDR3-005: "In Gate 1 the controller returns reads about 27-28 DDR clocks (270-280 ns) after issue on average, 60-70 at most, with HDMI scanout and AE350 traffic"
SDR-001: "Add-on SDRAM is Winbond W9825G6KH-6: 32 MB x16 per chip, 166 MHz grade, on dock connectors J9/J10"
AE350-001: "AE350 reset vector is fixed at 0x80000000; CPU-master extended AHB window at 0xE8000000 (0x08000000 long)"
AE350-002: "Caches reset disabled; mcache_ctl 0x7CA bit0 IC_EN, bit1 DC_EN; mcctlcommand 0x7CC value 6 = L1D write-back+invalidate all; micm/mdcm/mmsc_cfg at 0xFC0/0xFC1/0xFC2"
AE350-003: "The A25 L1 data cache is write-back; the 0xE8000000-0xEFFFFFFF peripheral window is uncached"
AE350-005: "With caches off, AE350 code running from the ROM port is instruction-fetch bound (fixed 784/1008 core cycles per loop iteration measured), masking memory latency"
AE350-006: "Gowin's timing model gives the AE350_SOC RAM-port (DDR_H*) inputs about 5 ns of setup at the macro; register every path into them"
AE350-004: "This board's A25: micm_cfg = mdcm_cfg = 0x00439ADA (32 KiB 4-way, 32 B lines, inferred), mmsc_cfg = 0x2007F039; fence.i makes D-cache stores visible to instruction fetch"
AE350-007: "The A25 core runs at the frequency of PLL_R[0] CLKOUT1, whatever the netlist connects to CORE_CLK; put the CPU clock on CLKOUT1"
AE350-008: "mcache_ctl bits 9 (IC_PREFETCH_EN) and 10 (DC_PREFETCH_EN) read back 0 after csrs on this board's A25: no hardware cache prefetch"
AE350-009: "A D-cache miss to DDR3 costs about 570 core cycles (760 ns) through the Gate 1 RAM path, with no overlap between misses; a hit costs about 3"
AE350-010: "A D-cache miss served by the 128 KiB fabric L2 costs about 240 core cycles; an L2 miss still costs about 570"
PSXCPU-001: "PlayStation CPU execution needs MIPS I integer/COP0 semantics, one branch delay slot, one load delay slot, and Cause.BD/EPC exception state"
GTE-001: "GTE is COP2; coordinate primitives include MVMVA, RTPS/RTPT, NCLIP, and AVSZ3/4 with fixed-point FIFOs and saturation flags"
GTE-002: "Lighting/color commands chain LLM, BK+LCM, RGBC multiply, and FC depth cue through 44-bit MACs and push MAC/16 to the color FIFO"
PSXGPU-001: "GPU uses GP0/GP1 at 1F801810h/1F801814h and 1024x512 16-bit VRAM; GP0 covers drawing, fills, copies, and CPU/VRAM transfers"
PSXIO-001: "BIOS startup uses mirrored 2 MiB RAM, scratchpad, 512 KiB BIOS, IRQ state, and DMA channels 2 (GPU) and 6 (OTC)"
PSXIO-002: "CD-ROM: 4 banked byte ports, INT1-5 handshake, BFRD reassertion preserves the FIFO cursor, licensed GetID 02 00 20 00 SCEx, 800h/924h sectors from byte 24/12"
PSXIO-003: "I_STAT bit 3 is raised on the 0-to-1 edge of DICR bit 31, not while a channel flag stays set"
PSXIO-004: "Digital pad on SIO0: host 01 42 00 00 00, pad FF 41 5A btnlo btnhi, /ACK after every byte but the last; empty ports return FF without /ACK"
TOOL-001: "gw_sh create_ipc/set_property/generate_target regenerate IP headlessly; read_ipc segfaults in batch mode"
TOOL-002: "GowinModGen -do <file>.mod regenerates PLL wrappers; PLL_INIT ships in IDE/ipcore/PLL_ADV/data/PLL/pll_init.v"
TOOL-003: "Gowin SDC cannot attach a clock to a net merged away by synthesis (TA2003); constrain the surviving PLL output net"
TOOL-004: "LiteX registers its Wishbone-to-CSR bridge only when the SoC has a LiteDRAM sdram core"
TOOL-005: "LiteX CSRStatus(fields=...) drives status from its field signals; drive the fields, not status"
TOOL-006: "LiteDRAMWishbone2Native's narrow-to-wide burst path compares full addresses combinationally to merge and cache; it fails 75 MHz on GW5AST behind the AE350"
TOOL-007: "SUG100 section 8.3: set_option -place_option 0-4, -route_option 0-2 select placement and routing algorithms; -replicate_resources 1 replicates high-fanout logic"
TOOL-008: "Upstream GNU Lightning's RISC-V backend is RV64-only; Tang-PSX's RV32 port is third_party/patches/gnu-lightning-rv32.patch; ww/d pairs are in memory order"
TOOL-009: "ilp32d doubles: fa0-fa7, then a GPR pair, a7+stack split, or an 8-aligned stack slot; variadic doubles use even GPR pairs and never split"
TOOL-010: "Lightrec exits only on block ends; JR/RFE re-runs a GTE command at EPC; cache isolation only from uncached code; JR-to-J keeps the caller's segment"
TOOL-011: "Run inside a Git work tree, git apply silently ignores patched paths outside the current directory and still succeeds; stop repository discovery to apply to an exported tree"
TOOL-012: "GNU Lightning RISC-V: lti/lti_u/gti/gti_u with a non-simm12 immediate compare against an unset temporary; conditional branches reach only simm12 bytes; master 7965700 is still RV64-only"
TCTL-001: "Uploads (put) are refused unless the TangCore main menu is active; cores load from cores/console138k/"
TCTL-002: "peek/poke use FPGA_EXT_READ32/WRITE32 over iosys_bl616 at 2,000,000 baud; core ID is reported by its low byte"
TCTL-003: "tangctl status reports core_running: no while a core is active; active_core (81 for Gate 1) is the reliable indicator"
TCTL-004: "Disc mailbox: firmware writes offset 0x204 and length 0x208, then advances sequence 0x200; Tang-Control answers with one stream session; disc size at 0x20c"
```

---

## 5. Records

```yaml
- record_id: DEV-001
  kind: DEVICE
  topic_id: DEV
  title: "Device revision identification; this board is revision C"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "Sipeed identifies the GW5AST device revision from the fifth character of the second marking line on the package. The installed device is marked GW5AST-LV138PG484AC1/I0, 2518CA0N, TS0E44.00; the fifth character is C."
  consequence: "Build for GW5AST-138C (Gowin set_device -name GW5AST-138C GW5AST-LV138PG484AC1/I0). Sipeed's example IP is generated for revision C and needs no revision conversion. Every image deployed through core-log entry 17 was built for revision C and ran."
  sources:
    - "Sipeed wiki, How to Identify Device Version: https://wiki.sipeed.com/hardware/en/tang/common-doc/questions.html#How-to-Identify-Device-Version (as cited by TangMega-138K-example ddr_memory/README.md, commit 06e7d8b118d345915ab6f257b7c22226f81575cd)"
  verification: "User photograph of the installed FPGA, 2026-09-28 (core-log entry 15); revision-C images configure and run on this board."

- record_id: DEV-002
  kind: DEVICE
  topic_id: DEV
  title: "GW5AST-138 logic and block-RAM resources"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "The device provides 138,240 LUTs, 139,095 registers, and 340 BSRAM blocks. Each BSRAM block is 18 Kbit, which is about 765 KB in total."
  consequence: "On-chip block RAM cannot hold the PSX's main RAM, VRAM, and sound RAM together (about 3.5 MB)."
  sources:
    - "Gowin EDA 1.9.11.03 place-and-route resource report for GW5AST-LV138PG484AC1/I0 (build/*/impl/pnr/*.rpt.txt, Resource Usage Summary)"
  verification: "Read from the tool's device totals in this project's builds. The 18 Kbit block size is from Gowin's BSRAM primitive family, not re-read from a datasheet here."

- record_id: BRD-001
  kind: BOARD
  topic_id: BRD
  title: "On-board DDR3 is x32: two x16 devices, 1 GiB"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "The PG484 SOM fits two Hynix H5TQ4G63EFR-RDC 4 Gbit x16 DDR3 devices on a shared address/command bus with distinct data lanes, forming DQ[31:0], DQS[3:0], and DM[3:0]; total capacity is 1 GiB. The LiteX board file and nand2mario's references use only the first device (x16) by choice, not because of routing."
  consequence: "The full x32 array is available; the 256-bit native port of a 1:4 x32 controller carries one BL8 burst."
  sources:
    - "Sipeed Tang Mega 138K PG484 SOM schematic tang_mega_138k_30354_Schematics..pdf, SHA-256 326c45a7e05e990d5f885fba6ec9b589d966941aa3c00f7d43122d99c068af7 (core-log entry 14)"
    - "Sipeed TangMega-138K-example, commit 06e7d8b118d345915ab6f257b7c22226f81575cd, ddr_memory/ddr_memory_test_uart/src/ddr3_1v4_hs.cst (Apache-2.0)"
  verification: "All 1 GiB written and verified on all four byte lanes (core-log entry 16); walking address bits across 1 GiB passed from the AE350 (entry 17). Two H5TQ4G63EFR-RDC packages are visible in the 2026-09-28 board photograph."

- record_id: BRD-002
  kind: BOARD
  topic_id: BRD
  title: "x32 DDR3 pin map (PG484)"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "All DDR3 pins are SSTL15 (differential SSTL15D for CK and DQS) with BANK_VCCIO=1.5, PULL_MODE=NONE, DRIVE=8. A[14:0] = D1 K1 K4 H3 L1 H5 J5 J1 G3 H2 J2 J4 G2 K2 M1 (A14 first). BA[2:0] = M6 P2 P5. CS_N P4, RAS_N L5, CAS_N L4, WE_N M5, CKE K6, ODT M2, RESET_N L6. CK = L3/K3. DM[3:0] = W1 T6 V7 AA4. DQS[3:0] (P/N) = R3/R2, W6/W5, V9/V8, Y3/AA3. DQ[7:0] = AB1 AB5 AB2 AA1 V4 AA5 AB3 Y4; DQ[15:8] = Y9 AB6 W9 AB8 Y7 AB7 Y8 AA8; DQ[23:16] = T5 V5 T4 Y6 R4 U6 R6 U5; DQ[31:24] = Y2 U1 V2 U2 U3 T1 Y1 W2 (MSB first in each group)."
  consequence: "Reference implementations: gateware/ddr3_vendor/tang_psx_ddr3.cst and gowin_ddr3.ddram32_io(). The Gowin controller also needs INS_LOC u_dll DDRDLLM_BL and fclkdiv LEFTSIDE[4], and its PLL at PLL_L[0]."
  sources:
    - "Sipeed TangMega-138K-example, commit 06e7d8b118d345915ab6f257b7c22226f81575cd, ddr_memory/ddr_memory_test_uart/src/ddr3_1v4_hs.cst"
  verification: "Hardware-proven by core-log entries 16 and 17."

- record_id: BRD-003
  kind: BOARD
  topic_id: BRD
  title: "Board clock and BL616 UART pins"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "A 50 MHz oscillator drives FPGA pin V22 (LVCMOS33). The BL616 debugger/control UART reaches the FPGA on V14 (FPGA receive) and U15 (FPGA transmit), LVCMOS33."
  consequence: "Tang-Control's iosys_bl616 transport uses these pins; Sipeed's example uses the same pins for its text UART."
  sources:
    - "litex-boards commit e4307929c38a, litex_boards/platforms/sipeed_tang_console.py (clk50, serial)"
    - "Sipeed TangMega-138K-example commit 06e7d8b, ddr3_1v4_hs.cst (clk, uart_tx, uart_rx)"
  verification: "Tang-Control transport and 50 MHz-derived clocks work in every deployed image."

- record_id: BRD-004
  kind: BOARD
  topic_id: BRD
  title: "Dock PMOD socket pins and numbering"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "The Tang Console dock's PMOD1 socket, the one beside the HDMI port, carries PMOD1_IO0-IO7 on FPGA pins W19 W20 F19 F20 E22 D22 E21 D21, and PMOD0 carries PMOD0_IO0-IO7 on V18 V19 G21 G22 F18 E18 C22 B22, as LVCMOS33. Sipeed's IO numbering interleaves the rows: IO0, IO2, IO4 and IO6 are PMOD pins 1-4 and IO1, IO3, IO5 and IO7 are pins 7-10. LiteX's pmod0/pmod1 connectors list the same FPGA pins in IO order, so a LiteX connector index is Sipeed's IO number, not the linear pin 1-4 then 7-10 order of the Digilent convention."
  consequence: "gateware/ae350_gate1.py maps the PmodVGA with J1 on PMOD1 and J2 on PMOD0 in interleaved order by default; gateware/vga_output.py keeps linear order and the other placements selectable at debug address 0x210."
  sources:
    - "TangCore commit f69c6ff, monitor/src/boards/console.cst (PMOD1_IO0-7 DualShock pins, PMOD0_IO0-7 LEDs) and nestang/src/boards/console60k_snescontroller.cst (one SNES controller on each of IO0/2/4 and IO1/3/5)"
    - "litex-boards commit e4307929c38a, litex_boards/platforms/sipeed_tang_console.py (_dock_connectors pmod0, pmod1)"
  verification: "A Digilent PmodVGA produced no sync in any placement with linear numbering and a correct picture with interleaved numbering and J1 on PMOD1 on 2026-09-29; the CRT then showed the framebuffer from power-on (core-log entry 37). The interleaving was inferred from TangCore's controller split before the hardware test; Sipeed's dock schematic was not available."

- record_id: BRD-005
  kind: EXTERNAL
  topic_id: BRD
  title: "Digilent PmodVGA pinout"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "The PmodVGA (rev C.0, Digilent 500-345) is a dual PMOD with 12-bit colour. J1 pins 1-4 are R0-R3 and pins 7-10 are B0-B3; J2 pins 1-4 are G0-G3, pin 7 is HS, pin 8 is VS and pins 9-10 are not connected; pins 5/11 are GND and 6/12 are VCC3V3 on both headers. Two SN74ALVC245 buffers powered from the PMOD 3.3 V pins, with 100 kOhm pulldowns on their inputs, drive per-colour resistor ladders in which bit 3 is the most significant step, and 100 Ohm series resistors on the syncs."
  consequence: "Every module pin is a buffered input, so a wrong placement only loses the picture. Turning the module over swaps pins 1-4 with 7-10 but keeps power and ground on their pins; reversing it end for end does not."
  sources:
    - "Digilent Pmod VGA Reference Manual, https://digilent.com/reference/pmod/pmodvga/reference-manual (pin table)"
    - "Digilent PmodVGA schematic rev C.0, doc 500-345, dated 2016-12-06"
  verification: "On a Dell E773c CRT the test pattern displayed and the Gate 1 framebuffer looked identical to the HDMI output to the user (core-log entry 37); the bar order and ramp steps were not separately confirmed."

- record_id: BRD-006
  kind: BOARD
  topic_id: BRD
  title: "FPGA module JTAG + UART connector U1201"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "The Tang Mega 138K module's 8-pin JST SH connector U1201 carries, by pin: 1 5V0 through diode D13 (8P_5V0), 2 TMS (BANK10 T13), 3 TDO (U13), 4 TCK (V12), 5 TDI (R13), 6 RX into the FPGA (BANK5 V14), 7 TX out of the FPGA (BANK5 U15), 8 GND. On the Tang Mega 138K docks the same JTAG nets run directly to the BL616 debugger (DBG_JTAG, with a 0 ohm resistor on TDO), and V14/U15 are the BL616 UART pins of BRD-003."
  consequence: "An external JTAG adapter shares TCK, TMS and TDI with the BL616, which drives them while it loads a core, so the adapter must be released or unplugged then; pins 6 and 7 must stay unconnected because they carry the Tang-Control transport. Pin 1 is about 4.4 V and must not reach a 3.3 V adapter. The connector reaches the FPGA's configuration JTAG, not the AE350's debug JTAG."
  sources:
    - "Sipeed tang_mega_138k_30353_Schematics.pdf (TANG_MEGA_138K_30353), sheet 'JTAG Connector', U1201; Sipeed Tang_Mega_NEO_Dock-138K_31005_Schematics.pdf, USB_JTAG (BL616) sheet"
  verification: "Unloaded voltages on 2026-09-29 matched the assignment (GND, 3.37, 3.29, 3.36, 1.64, 1.22, 3.36, 4.44 V from pin 8 to pin 1), and a Raspberry Pi Pico 2 running lonehog/JTAGprobe (CMSIS-DAP v2, TCK GP19, TMS GP14, TDI GP18, TDO GP21) under OpenOCD 0.12.0 found one TAP with IDCODE 0x0001081B (Gowin, IR length 3) (core-log entry 44). openFPGALoader 0.13.1 did not find the probe, because its cmsisdap driver needs CMSIS-DAP v1 HID."

- record_id: DDR3-001
  kind: MEMORY
  topic_id: DDR3
  title: "Proven Gowin DDR3 controller configuration"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "Controller (not PHY-only) interface, DDR3 components, 400 MHz memory clock (800 MT/s), 1:4 clock ratio, DQ width 32, DRAM width 16, one rank, 15 row and 10 column bits, burst 8 sequential, CL 6, CWL 5, AL 0, RTT_NOM 40 ohm, RTT_WR off, 1T address/command, high output drive, controller-managed refresh. Timing in ps: tRTP 7500, tRP 15000, tWTR 7500, tRC 55000, tRAS 37500, tRCD 15000, tFAW 40000, tRRD 10000, tCKE 10000, tREFI 7800000, tRFC 260000; tDLLK 512 clocks. The 400 MHz memory clock comes from a PLL at 50 MHz x16 / 2 (800 MHz VCO) with dynamic ICP/LPF selection driven by Gowin's PLL_INIT. The controller's user clock (clk_out) is 100 MHz and its user data port is 256 bits."
  consequence: "Committed as gateware/ddr3_vendor/ddr3_ip.tcl and ddr3_memory_interface.ipc. Measured streaming throughput from fabric is about 2.85 GB/s write and 2.93 GB/s read, against a 3.2 GB/s theoretical peak; calibration takes about 27-29 ms."
  sources:
    - "Sipeed TangMega-138K-example commit 06e7d8b, ddr_memory/ddr_memory_test_uart/src/ddr3_memory_interface/ddr3_memory_interface.ipc and gowin_pll/gowin_pll.mod (Apache-2.0)"
  verification: "232 full-array passes with zero errors (core-log entry 16); AE350 Gate 1 checks pass through it (entry 17)."

- record_id: DDR3-002
  kind: MEMORY
  topic_id: DDR3
  title: "Gowin DDR3 native (Controller) user-port protocol"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "cmd = 3'b000 is write and 3'b001 is read. A command is taken when cmd_en and cmd_ready are both high. With burst tied to 0, each command moves one 256-bit word: write data is presented with wr_data_en and wr_data_end high in the same cycle as its write command, which must also have wr_data_rdy high. wr_data_mask is active-high: a 1 bit leaves that byte unwritten. Reads return in command order on rd_data with rd_data_valid, and the port has no read-ready input, so read data cannot be back-pressured. sr_req and ref_req may be tied low when User_Refresh is off."
  consequence: "Any front end must hold write commands until their data is available and must guarantee buffer space for every outstanding read (see gateware/ddr3_vendor/gowin_ddr3_native.sv)."
  sources:
    - "Gowin EDA 1.9.11.03 DDR3 IP generated instantiation template (ddr3_memory_interface_tmp.v) and IDE/ipcore/DDR3/data/ddr3_1_4code_hs/DDR3_TOP.v port list"
    - "Sipeed TangMega-138K-example commit 06e7d8b, ddr_memory/ddr_memory_test_uart/src/ddrtest.v (usage example)"
  verification: "Command encoding, same-cycle write data, and in-order reads were proven by the entry 16 full-array test. Mask polarity was proven by the entry 17 AE350 byte and halfword test. The no-back-pressure property is structural: the generated port list has no read-ready input. Gowin's IP user guide (IPUG281) was not consulted for this record."

- record_id: DDR3-003
  kind: MEMORY
  topic_id: DDR3
  title: "Native-port address layout"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "addr[28:0] counts 32-bit (DQ-width) words. One BL8 command covers 8 consecutive words, so burst addresses advance by 8 and addr[2:0] is 0. The IP reports addr[28] (the rank bit) as unused for this one-rank configuration; addr[27:0] spans the full 1 GiB."
  consequence: "A 256-bit native-word index n maps to addr = {1'b0, n[24:0], 3'b000}."
  sources:
    - "Gowin EDA 1.9.11.03 DDR3 IP generation log: 'Input addr[28] is unused' (build/*/ip/ddr3_memory_interface/temp/DDR3/ddr3_memory_interface.log)"
  verification: "The full-array test with sequence-dependent data (entry 16) and walking address bits to 1 GiB from the AE350 (entry 17) found no aliasing."

- record_id: DDR3-004
  kind: MEMORY
  topic_id: DDR3
  title: "DDR3 IP versions: Gowin EDA 1.9.11.03 vs 1.9.12.02_SP1"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "Gowin EDA 1.9.11.03 ships DDR3 Memory Interface IP 5.9 (IDE/ipcore/DDR3/ddr3.ipspec). Sipeed generated its example with 1.9.12.02_SP1 (IP 6.0). Version 6.0 adds the ARBITRATION_MODE, AXI4_INTERFACE, AXI_*, ID_WIDTH, and MC_* (BSRAM/register/SSRAM FIFO selection) options, which 5.9 lacks, and 6.0 emits `define EBR_BASED. Version 5.9 emits WR_CYC and DBG_PARAMETER1-4. Every option common to both produces an identical .ipc and an identical port list. At this configuration 5.9 reports WRITE_RECOVERY as a disabled option and keeps its default."
  consequence: "Staying on 1.9.11.03 means no AXI user interface on the controller; the native port is the only user interface."
  sources:
    - "Gowin EDA 1.9.11.03 IDE/ipcore/DDR3/ddr3.ipspec (version 5.9) and libDDR3.so parameter templates"
    - "Sipeed TangMega-138K-example commit 06e7d8b, ddr3_memory_interface/temp/DDR3/{gwmc_param.v,DDR3_define.v} and ddr3_memory_interface_tmp.v (Tool Version V1.9.12.02_SP1, IP Version 6.0)"
  verification: "Regenerated and diffed locally on 2026-09-28 (core-log entry 16)."

- record_id: DDR3-005
  kind: MEMORY
  topic_id: DDR3
  title: "Gowin controller read latency in Gate 1"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "Counted in gowin_ddr3_native.sv from a read's issue to the controller's read data, over 13.7 to 69.8 million reads including HDMI scanout, AE350 and fabric GPU traffic, the average was 27.1 to 28.3 clocks of the 100 MHz user clock and the maximum 60 to 70."
  consequence: "About 21 system cycles of the roughly 57-cycle AE350 miss are the controller; with the fabric path's 23 cycles to the last beat (core-log entry 43) the remaining 13 or so are AE350-internal handling and waits behind other clients. The 32-bit latency sum wraps after about 150 million reads, so averages must come from nearby snapshots."
  sources:
    - "gateware/ddr3_vendor/gowin_ddr3_native.sv latency counters, read through debug addresses 0x230-0x238"
  verification: "Hardware readings on 2026-09-29 (core-log entry 44)."

- record_id: SDR-001
  kind: MEMORY
  topic_id: SDR
  title: "SDRAM add-on part and connector wiring"
  status: SOURCED
  verified_date: 2026-09-28
  statement: "The add-on module carries Winbond W9825G6KH-6 SDR SDRAM: 256 Mbit (32 MB) x16 per chip, -6 speed grade (166 MHz), 3.3 V I/O. The dock exposes two 40-pin SDRAM connectors, J9 (sdram0_connector) and J10 (sdram1_connector), each carrying one 16-bit SDRAM with A[12:0], BA[1:0], DQ[15:0], DQM[1:0], CS_N, RAS_N, CAS_N, WE_N, and CLK on connector pin 20."
  consequence: "Up to 64 MB if both connectors are populated. Not yet exercised by this project; TangCore cores drive it on the Console (tangcore nestang/src/boards/console.cst)."
  sources:
    - "litex-boards commit e4307929c38a, litex_boards/platforms/sipeed_tang_console.py (sdram0/1_connector, sipeedSDRAM(), W9825G6KH6 module selection in targets/sipeed_tang_console.py)"
    - "Chip markings W9825G6KH-6 visible in the 2026-09-28 board photograph"
  verification: "Part confirmed by photograph only. Which connectors are populated, and the connector pin mapping, are not hardware-verified."

- record_id: AE350-001
  kind: PROCESSOR
  topic_id: AE350
  title: "AE350 reset vector and fabric bus windows"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "The AE350 hard CPU (RV32IMAFDC, Sv32 MMU per the LiteX wrapper) fetches its reset code at the fixed address 0x80000000 over its ROM AHB port. Its CPU-master extended AHB slave window (EXTS) is 0xE8000000-0xEFFFFFFF, and its 64-bit RAM AHB port (DDR_H*) carries all other memory traffic. The internal peripherals and APB extension are not on the fabric bus."
  consequence: "Firmware is linked to execute from 0x80000000. LiteX places CSRs at 0xE8000000 in the EXTS window and main RAM at 0x40000000 on the RAM port."
  sources:
    - "LiteX commit 9fa53dc19a22, litex/soc/cores/cpu/gowin_ae350/core.py (reset_address, io_regions citing Gowin MUG1029 Table 3-1)"
  verification: "Firmware boots from 0x80000000, CSR accesses at 0xE8000000 work, and the RAM port reaches DDR3 at 0x40000000 (core-log entries 2 and 17). Gowin MUG1029 itself was not re-read for this record."

- record_id: AE350-002
  kind: PROCESSOR
  topic_id: AE350
  title: "AndeStar V5 cache control CSRs"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "The AE350's A25 core resets with its L1 instruction and data caches disabled. mcache_ctl (CSR 0x7CA): bit 0 IC_EN, bit 1 DC_EN, bit 3 IC_ECCEN, bit 5 DC_ECCEN, bit 8 CCTL_SUEN (allow CCTL from S/U mode), bit 9 IC_PREFETCH_EN, bit 10 DC_PREFETCH_EN, bit 13 DC_WAROUND_EN, bit 15 L2C_WAROUND_EN, bit 19 DC_COHEN, bit 20 DC_COHSTA. mcctlcommand (CSR 0x7CC) executes a cache-control (CCTL) operation; command 6 is L1D_WBINVAL_ALL (write back and invalidate the whole L1 D-cache). ucctlcommand is CSR 0x80C. Read-only configuration: micm_cfg 0xFC0 (I-cache; size field ISZ at bits 8:6, 0 means no cache), mdcm_cfg 0xFC1 (D-cache; DSZ at bits 8:6), mmsc_cfg 0xFC2 (bit 16 CCTLCSR means the CCTL CSRs exist; bit 30 PPMA means programmable PMA). mmisc_ctl is 0x7D0 (bit 8 NON_BLOCKING_EN)."
  consequence: "Firmware enables the caches with csrs 0x7ca, 0x3, confirms the bits by readback, checks mmsc_cfg.CCTLCSR before issuing CCTL, and writes back and invalidates the D-cache (csrw 0x7cc, 6) before any check that must read DDR3 rather than the cache."
  sources:
    - "Andes Technology code in upstream U-Boot, commit 1d29c718b7ba09807f8060796d9c21772e3c1b52: arch/riscv/include/asm/arch-andes/csr.h, arch/riscv/cpu/andes/cache.c, arch/riscv/cpu/andes/cpu.c (https://github.com/u-boot/u-boot)"
    - "OpenSBI, commit 06af8bd61b37bbf0db93adfa9902b176d0158ea1: platform/generic/include/andes/andes.h, platform/generic/andes/ae350.c (https://github.com/riscv-software-src/opensbi)"
    - "enjoy-digital litex_wr_nic PR #103, merge 05df6f4e349855cd0a695e6173834fe5509c954c: doc/tang_mega_138k_pro.md and litex_wr_nic/firmware/build.py (caches reset disabled; enabled with csrs 0x7ca, 0x3 on a GW5AST-138B AE350)"
  verification: "Confirmed on this board on 2026-09-28 (core-log entry 18). csrs 0x7ca, 0x3 read back as 0x3; mmsc_cfg.CCTLCSR was set; csrw 0x7cc, 6 before each verify pass let DDR3 checks pass with the write-back D-cache on. The field layouts of micm_cfg and mdcm_cfg beyond the size fields were not found in a primary source (see AE350-004), and the AndeStar V5 System Privileged Architecture specification was not consulted."

- record_id: AE350-003
  kind: PROCESSOR
  topic_id: AE350
  title: "AE350 cacheability and write policy"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "The A25 L1 data cache is write-back. Addresses in the AE350's peripheral range, which includes the extended AHB window at 0xE9000000 and so the whole 0xE8000000-0xEFFFFFFF EXTS window, are uncached, so fabric registers there stay coherent with the caches enabled. Memory on the ROM (0x80000000) and RAM (fabric SRAM at 0x0, DDR3 at 0x40000000) ports is cacheable once the caches are enabled."
  consequence: "LiteX CSRs at 0xE8000000 need no cache maintenance. Data that fabric masters or the Tang-Control debug bus read out of DDR3 or SRAM must be written back first, and data they write there must be invalidated before the CPU reads it."
  sources:
    - "enjoy-digital litex_wr_nic PR #103, merge 05df6f4e349855cd0a695e6173834fe5509c954c: doc/tang_mega_138k_pro.md ('0xe9000000, inside the CPU's uncached peripheral range ... the A25's data cache is write-back')"
  verification: "Secondary source that was hardware-tested on a GW5AST-138B AE350. On this board with both caches enabled (core-log entry 18), CSR writes and reads at 0xE8000000 (stage, log, and status registers) remained immediately visible to the Tang-Control debug bus and the firmware, which is consistent with the window being uncached. The write-back policy itself was not isolated on this board, and the exact bounds of the uncached range were not read from Gowin MUG1029 or AE350 documentation."

- record_id: AE350-004
  kind: PROCESSOR
  topic_id: AE350
  title: "Cache configuration and fence.i behavior observed on this board"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "On this board's AE350, micm_cfg (0xFC0) and mdcm_cfg (0xFC1) both read 0x00439ADA and mmsc_cfg (0xFC2) reads 0x2007F039. The size fields (bits 8:6) are nonzero, so both L1 caches are present. mmsc_cfg bit 16 (CCTLCSR) is set and bit 30 (PPMA) is clear, so no programmable PMA is available. With both caches enabled, code that has been executed and then rewritten by stores without fence.i still executed the old instructions; after fence rw,rw plus fence.i it executed the new instructions."
  consequence: "Runtime code generation must issue fence.i (after its stores) before executing newly written code; with that, no explicit D-cache write-back is needed for instruction-fetch coherency. With no programmable PMA, cacheability is fixed by the AE350 address map (AE350-003)."
  sources:
    - "RISC-V Unprivileged ISA, Zifencei extension (FENCE.I synchronizes instruction and data streams on the executing hart)"
    - "Field offsets of ISZ/DSZ, CCTLCSR, and PPMA: OpenSBI commit 06af8bd61b37, platform/generic/include/andes/andes.h"
  verification: "Firmware readback and the code-execution probe on 2026-09-28 (core-log entry 18). Decoding 0x00439ADA as ISET=2 (256 sets), IWAY=3 (4 ways), ISZ=3 (32-byte lines), giving 32 KiB, is INFERRED from the AndeStar V5 field order and is not confirmed by a primary document."

- record_id: AE350-005
  kind: PROCESSOR
  topic_id: AE350
  title: "Uncached AE350 code is instruction-fetch bound"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "With the L1 caches disabled (their reset state), a simple store loop and a load loop running from the ROM port took exactly 784 and 1,008 core cycles per iteration. The totals were identical to the cycle across two builds whose DDR3 paths differed in pipeline depth, so the time is set by fetching the loop's instructions over the ROM AHB port, not by data-memory latency."
  consequence: "Enable the caches before any performance measurement. Uncached cycle counts from ROM-resident code do not measure DDR3 or SRAM speed."
  sources:
    - "enjoy-digital litex_wr_nic PR #103 (merge 05df6f4e), doc/tang_mega_138k_pro.md: 'fetching WRPC over the AHB port is then the bottleneck'"
  verification: "Inferred from identical uncached counts in core-log entries 18 and 19 (205,521,436 and 264,241,456 core cycles per 1 MiB) despite different DDR3 paths."

- record_id: AE350-006
  kind: PROCESSOR
  topic_id: AE350
  title: "AE350 RAM-port input timing"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "In Gowin EDA 1.9.11.03 timing analysis for GW5AST-138C, the AE350_SOC macro's RAM-port AHB inputs (DDR_HRDATA, DDR_HREADY and related) account for about 5 ns of a path's delay at the macro (for example, arrival 14.649 ns to endpoint 19.709 ns). That is over a third of the 13.333 ns 75 MHz period."
  consequence: "Drive the AE350 RAM-port inputs from registers or very shallow logic. Gate1RAMBridge registers its DDR3 Wishbone responses for this reason."
  sources:
    - "Gowin EDA 1.9.11.03 place-and-route timing paths (build/gate1-place*/gateware/impl/pnr/project.timing_paths), AE350_SOC endpoints"
  verification: "Read from this project's timing reports (core-log entries 17 and 19)."

- record_id: AE350-007
  kind: PROCESSOR
  topic_id: AE350
  title: "The AE350 core clock is PLL_R[0] CLKOUT1"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "Gowin documents AE350_SOC CORE_CLK as a dedicated clock path of up to 800 MHz, and the AE350 PLL must be placed at PLL_R[0]. On this board the A25 ran at 75.0 MHz when the PLL produced 750 MHz on CLKOUT0 (connected to CORE_CLK in the netlist) and 75 MHz on the unconnected CLKOUT1; changing only CLKOUT0 to 375 MHz and CLKOUT1 to 50 MHz moved the core to 49.85 MHz; and generating 750 MHz on CLKOUT1 and connecting it to CORE_CLK ran the core at 750 MHz. The main system PLL's 75 MHz output and the 50 MHz board clock were unchanged throughout, which excludes them as the source."
  consequence: "Generate the CPU clock on PLL_R[0] CLKOUT1 and connect CORE_CLK to it (gateware/ae350_pll.v). Gowin timing analysis constrains the CORE_CLK net as declared and does not detect the mismatch. AE350 cycle counts recorded before core-log entry 26, including AE350-005, were taken with a 75 MHz core. Tang-Phosphor's src/ae350/ae350_pll.v still drives CORE_CLK from CLKOUT0."
  sources:
    - "Gowin EDA 1.9.11.03, IDE/simlib/gw5a/prim_sim.v: AE350_SOC port comments ('CPU core clock, up to 800MHz, dedicated clock path') and PLL output-divider model"
    - "litex-boards commit e4307929c38a, litex_boards/targets/sipeed_tang_mega_138k_pro.py: INS_LOC PLL_R[0] for the Gowin AE350"
    - "Tang-Phosphor commit 292ae779da23, src/ae350/ae350_pll.v (CLKOUT0 = 750 MHz, CLKOUT1 = 75 MHz)"
  verification: "Inferred from three hardware builds on 2026-09-28 (core-log entry 26). software/programs/clock ran 2^31 and 2^30 counted cycles of a dependent addi chain at 0.94 to 0.99 instructions per cycle; tools/ae350_run.py wall-clock times of 28.69 s, 21.54 s, and 1.48 s give 74.85, 49.85, and at least 725 MHz. No primary document naming CLKOUT1 was found."

- record_id: AE350-008
  kind: PROCESSOR
  topic_id: AE350
  title: "This board's A25 has no hardware cache prefetch"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "Setting mcache_ctl (CSR 0x7CA) bits 9 (IC_PREFETCH_EN) and 10 (DC_PREFETCH_EN, AE350-002) with csrs from a program loaded by the Gate 1 ROM, whose caches are already enabled, leaves the register reading 0x3: both prefetch enables read as zero, so this A25 does not implement them."
  consequence: "Cache misses to DDR3 through the RAM bridge cannot be hidden by the core's own prefetch; reducing CPU-side miss cost needs a faster RAM path or better locality."
  sources:
    - "AE350-002 (mcache_ctl bit assignments)"
  verification: "software/programs/psx_disc built with the csrs and a read-back logged mcache_ctl 3 on hardware on 2026-09-29 (core-log entry 41)."

- record_id: AE350-009
  kind: PROCESSOR
  topic_id: AE350
  title: "AE350 D-cache miss cost to DDR3 through the Gate 1 RAM path"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "With the loader's cache settings (mcache_ctl 0x3) on the entry 37 core, software/programs/memlat measured, in 750 MHz core cycles per access: 3.2 for dependent loads within 16 KiB; 568 and 570 for dependent loads over random 32-byte lines of 256 KiB and 4 MiB; 552 per line for consecutive independent loads over 4 MiB, so misses do not overlap; and 637 per line for stores over 4 MiB, including write-back of the evicted dirty line."
  consequence: "A miss costs about 760 ns, about 57 cycles of the 75 MHz system clock, of which moving a 32-byte line over the 64-bit RAM port needs four; the rest is latency in the RAM bridge, the burst converter, the 75-to-100 MHz crossing, the arbiters and the Gowin controller. Reducing it, or serving misses from an FPGA-side cache, speeds up all AE350 code without changing its results."
  sources:
    - "software/programs/memlat/main.c"
  verification: "Measured on hardware on 2026-09-29 (core-log entry 42)."

- record_id: AE350-010
  kind: PROCESSOR
  topic_id: AE350
  title: "AE350 miss cost with the fabric L2"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "With gateware/l2_cache.py (128 KiB, direct-mapped, write-through) on the AE350 RAM port, software/programs/memlat measured dependent loads over 64 KiB and 96 KiB, which fit the L2 but not the 32 KiB D-cache, at 240.0 core cycles each with the L2 enabled and 569.6 to 569.7 with it disabled; 256 KiB and 4 MiB stayed at about 570 either way, and streaming loads and stores were about 1 percent slower with it enabled."
  consequence: "An L2 hit costs about 24 system cycles against the simulation's 16 for the fabric path (core-log entry 43), so about 8 system cycles lie in the AE350 and the register slice's three cycles per beat; the extra tag-lookup cycle is not measurable on a miss."
  sources:
    - "software/programs/memlat/main.c; gateware/l2_cache.py"
  verification: "Measured on hardware on 2026-09-29 with the L2 switched at debug address 0x218 (core-log entry 44)."

- record_id: PSXCPU-001
  kind: PROCESSOR
  topic_id: PSXCPU
  title: "PlayStation R3000A-compatible execution semantics"
  status: SOURCED
  verified_date: 2026-09-28
  statement: "The PlayStation CPU uses the MIPS I integer instruction set and R3000 pipeline behavior. A jump or branch is followed by one executed delay-slot instruction. Integer and coprocessor loads have a one-instruction result delay. On an exception in a branch delay slot, Cause.BD is set and EPC identifies the branch rather than the delay-slot instruction; otherwise EPC identifies the faulting instruction. The R3000 CP0 exception codes used here are AdEL 4, AdES 5, IBE 6, DBE 7, Syscall 8, Break 9, Reserved Instruction 10, Coprocessor Unusable 11, and Arithmetic Overflow 12."
  consequence: "software/psx/r3000.c preserves the branch and load delays, CP0 Cause/EPC/BadVAddr state, arithmetic and address exceptions, little-endian memory operations, HI/LO, and COP2 transfers needed by the initial interpreter diagnostic."
  sources:
    - "MIPS Computer Systems, R3000 User's Manual, https://usermanual.wiki/Document/r3000manual.723589236.pdf"
    - "PCSX-Redux commit 80d78dd693be4d8c5fd832825d934c5e59e0a6dd, src/core/psxinterpreter.cc and src/core/r3000a.{cc,h}, https://github.com/grumpycoders/pcsx-redux"
  verification: "The independently generated nine-vector CPU regression passed 65 selected state checks natively and on the AE350 in core-log entry 23. This is not validation against original PlayStation silicon, so the record remains SOURCED."

- record_id: GTE-001
  kind: PROCESSOR
  topic_id: GTE
  title: "GTE COP2 registers and coordinate commands"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "The GTE is accessed as COP2 through 32 data and 32 control registers. Its coordinate-command set includes MVMVA (matrix/vector multiply and translation), RTPS and RTPT (single/triple perspective transforms), NCLIP (signed screen-space triangle area), and AVSZ3/AVSZ4 (depth averaging). Results pass through signed IR and MAC registers plus screen-coordinate and depth FIFOs, with command-specific saturation recorded in FLAG. MFC2 and CFC2 have a one-instruction GPR load delay."
  consequence: "software/psx/gte.c implements the register transfers and these six coordinate-operation families as the first software-GTE subset. It does not yet claim complete lighting/color commands, command latency, or all overflow and saturation edge cases."
  sources:
    - "PSX-SPX, Geometry Transformation Engine (GTE), https://psx-spx.consoledev.net/ps1/cpu/gte/geometrytransformationenginegte/"
    - "PCSX-Redux commit 80d78dd693be4d8c5fd832825d934c5e59e0a6dd, src/core/gte-instructions.cc, gte-internal.h, and gte-transfer.cc, https://github.com/grumpycoders/pcsx-redux"
  verification: "Six generated GTE vectors covering normal-range MVMVA, RTPS, RTPT, NCLIP, AVSZ3, and AVSZ4 produced 31 expected register results both natively and on the AE350 in core-log entry 23. No original PlayStation hardware comparison was made, so this reverse-engineered behavior remains INFERRED."

- record_id: GTE-002
  kind: PROCESSOR
  topic_id: GTE
  title: "GTE lighting and color commands"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "NCS/NCT light a normal: [IR]=[MAC]=(LLM*V) SAR (sf*12), then [IR]=[MAC]=(BK*1000h+LCM*IR) SAR (sf*12), and push [MAC1/16,MAC2/16,MAC3/16,CODE] to the color FIFO. NCCS/NCCT and CC then form [R*IR1,G*IR2,B*IR3] SHL 4 from RGBC before shifting and pushing; NCDS/NCDT, CDP, and DCPL additionally depth-cue that product toward the far color FC by IR0 (MAC+(FC-MAC)*IR0); DPCS/DPCT depth-cue [R,G,B] SHL 16 (DPCT taking its three colors from RGB0), INTPL depth-cues [IR] SHL 12, GPF computes IR*IR0, GPL MAC SHL (sf*12)+IR*IR0, SQR IR*IR, and OP the cross product of IR with the RT diagonal. FLAG bits 30-25 report 44-bit MAC overflow, 24-22 IR saturation, 21-19 color-FIFO saturation to 00h-FFh, and bit 31 is the OR of bits 30-23 and 18-13."
  consequence: "software/psx/gte.c implements all sixteen commands. The FC-MAC step saturates IR with lm=0 regardless of the command's lm bit, as PCSX-Redux does; command latency and intermediate per-product overflow checks are not modeled."
  sources:
    - "PSX-SPX, Geometry Transformation Engine (GTE), https://psx-spx.consoledev.net/ps1/cpu/gte/geometrytransformationenginegte/ (color and general-purpose calculation commands, FLAG register)"
    - "PCSX-Redux commit 80d78dd693be4d8c5fd832825d934c5e59e0a6dd, src/core/gte-instructions.cc"
  verification: "The SCPH-1001 PlayStation logo (NCDS) and Spyro the Dragon's first screen rendered as expected on the host and the AE350 (core-log entry 27), and the 96 CPU/GTE reference checks still pass. No bit-level comparison with original hardware was made."
- record_id: PSXGPU-001
  kind: PROCESSOR
  topic_id: PSXGPU
  title: "PlayStation GPU ports, command stream, and VRAM"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "The PlayStation GPU is accessed through GP0/GPUREAD at 1F801810h and GP1/GPUSTAT at 1F801814h. It owns 1024x512 16-bit VRAM. GP0 packets control drawing environment, polygons, lines, rectangles, fills, VRAM copies, and CPU-to-VRAM or VRAM-to-CPU image transfers; GP1 controls reset, command buffering, DMA direction, display enable, display origin/ranges/mode, and status queries. Native pixels use 5-bit red, green, and blue fields, with bit 15 used for masking/semi-transparency state."
  consequence: "software/psx/gpu.c parses the startup subset of GP0/GP1, rasterizes into BGR555 VRAM, and exposes display state for conversion into Gate 1's RGB565 framebuffer. Unimplemented texture-window details, polyline packets, blending accuracy, dithering, and timing remain outside the hardware-proven logo checkpoint."
  sources:
    - "PSX-SPX, GPU I/O Ports, DMA Channels, Commands, VRAM, https://psx-spx.consoledev.net/ps1/gpu/i-o-ports-dma-channels-commands-vram/"
    - "PSX-SPX, GPU Status Register, https://psx-spx.consoledev.net/ps1/gpu/status-register/"
    - "PCSX-Redux commit 80d78dd693be4d8c5fd832825d934c5e59e0a6dd, src/core/gpu.{cc,h}, src/core/psxhw.cc, and src/gpu/soft, https://github.com/grumpycoders/pcsx-redux"
  verification: "The SCPH-1001 ROM issued 10,768 accepted GPU words with zero unknown commands, 414 primitives, and 63 image uploads; the native regression produced a deterministic framebuffer and the AE350 displayed the matching complete startup logo in core-log entry 24. No comparison against original GPU electrical timing or pixel-edge behavior was made, so the record remains INFERRED."

- record_id: PSXIO-001
  kind: PROCESSOR
  topic_id: PSXIO
  title: "PlayStation BIOS-visible memory, interrupt, and DMA map"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "The startup-visible map includes 2 MiB main RAM mirrored through the first 8 MiB, 1 KiB scratchpad at 1F800000h, memory and peripheral registers beginning at 1F801000h, and a 512 KiB BIOS at 1FC00000h. I_STAT/I_MASK aggregate peripheral interrupts. DMA channel 2 moves GPU block and linked-list streams, while channel 6 constructs the reverse ordering table; DICR records channel completion and can raise the DMA interrupt."
  consequence: "software/psx/machine.c supplies these mappings plus the startup register behavior for memory control, timers, CD-ROM, and SPU, generates VBlank interrupt state, and implements GPU and OTC DMA. It is a startup-focused model rather than a cycle-accurate peripheral implementation."
  sources:
    - "PSX-SPX, Memory Map, https://psx-spx.consoledev.net/ps1/kernelbios/memory-map/"
    - "PSX-SPX, I/O Map, https://psx-spx.consoledev.net/ps1/system/iomap/"
    - "PSX-SPX, DMA Channels, https://psx-spx.consoledev.net/ps1/system/dmachannels/"
    - "PCSX-Redux commit 80d78dd693be4d8c5fd832825d934c5e59e0a6dd, src/core/psxmem.cc, psxhw.cc, and psxdma.{cc,h}, https://github.com/grumpycoders/pcsx-redux"
  verification: "The SCPH-1001 ROM completed the approved logo checkpoint on the AE350 after 99,999,544 interpreted instructions, 158,497 DMA words, and 199 modeled VBlanks, with zero unknown I/O accesses in the host regression and matching terminal hardware telemetry in core-log entry 24. Peripheral accuracy beyond that command path has not been compared with original hardware, so the record remains INFERRED."

- record_id: PSXIO-002
  kind: PROCESSOR
  topic_id: PSXIO
  title: "CD-ROM controller registers, commands, and sector delivery"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "The controller is four byte ports at 1F801800h-1F801803h, banked by the index in 1F801800h bits 1:0. Reading 1F801800h returns the index with PRMEMPT (bit 3), PRMWRDY (bit 4), RSLRRDY (bit 5), DRQSTS (bit 6), and BUSYSTS (bit 7). Commands are written to 1F801801h bank 0 with parameters at 1F801802h bank 0. Results are read from 1F801801h. Interrupt enable is 1F801802h bank 1, and the interrupt type is read from 1F801803h banks 1/3 (INT1 data ready, INT2 complete, INT3 acknowledge, INT4 data end, INT5 error), acknowledged by writing 1F801803h bank 1, whose bit 6 clears the parameter FIFO. Writing bit 7 (BFRD) of 1F801803h bank 0 loads the current sector buffer into the data FIFO read by 1F801802h or DMA channel 3; writing BFRD=1 again while it is already set preserves the current FIFO cursor, while clearing it resets the cursor. Each command first answers INT3; Init, MotorOn, Stop, Pause, SeekL/P, SetSession, ReadTOC, and GetID later answer INT2 (GetID for a licensed Mode 2 disc: 02 00 20 00 followed by SCEA, SCEE, or SCEI). A status byte reports Error, Motor, SeekError, IdError, ShellOpen, Read, Seek, and Play. Setmode bit 7 selects double speed (75 or 150 sectors per second) and bit 5 the sector size: 800h delivers the 2048 data bytes (raw offset 24 for Mode 2 Form 1), 924h the 2340 bytes after the 12 sync bytes. Init sets the mode to 20h."
  consequence: "software/psx/cdrom.c implements this controller with 1x/2x sector timing, a response queue that waits for each acknowledge, holding of a sector until the CPU acknowledges INT1, preservation of the FIFO cursor across repeated BFRD enable writes, and dropping of real-time XA audio sectors when ADPCM is enabled. PsyQ LibCD depends on the cursor rule: in 924h mode it reads three header words, reasserts BFRD, and then transfers 512 data words. CD-DA, XA-ADPCM playback, and exact response latencies are not modeled; the acknowledge (20,000 cycles), completion, and seek delays are approximations."
  sources:
    - "PSX-SPX, CDROM Drive, https://psx-spx.consoledev.net/ps1/cdr/cdromdrive/ (register map, status, interrupt, command, Setmode, GetID, and Getparam/GetlocL/GetlocP sections, fetched 2026-09-28)"
    - "DuckStation commit bfb23fb8c94706e7a11d614157b6b47c938f9c59, src/core/cdrom.cpp (request-register and sector-buffer cursor behavior), https://github.com/stenzek/duckstation"
    - "TheMobyCollective Spyro 1 decompilation commit 94153310870f612faea9aea6c782363c73593ba3, asm/psyq.s and src/cd.c (PsyQ CdRead header/data sequence), https://github.com/TheMobyCollective/spyro-1"
  verification: "On the host and on the AE350 (core-log entries 27 and 28), SCPH-1001 accepted a licensed US disc image through this controller, read the license and logo sectors, and booted SCUS_942.28 from the ISO file system. Entry 28's focused two-sector regression verifies that repeated BFRD enable writes preserve the header-to-data cursor, and Spyro continued into its polygon intro on hardware instead of corrupting low RAM during the Universal-logo load. No original PlayStation hardware comparison was made."

- record_id: PSXIO-003
  kind: PROCESSOR
  topic_id: PSXIO
  title: "DMA interrupt edge"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "DICR (1F8010F4h) bit 31 is the DMA master interrupt flag, set while bit 15 forces it or bit 23 enables and an enabled channel flag (bits 30:24) is set. I_STAT bit 3 is set when bit 31 changes from 0 to 1, not continuously while it stays set."
  consequence: "Raising I_STAT bit 3 on every DICR update while another channel flag stays set produces an endless DMA interrupt loop in the SCPH-1001 kernel's CD DMA handler; machine.c raises it only on the rising edge."
  sources:
    - "PSX-SPX, DMA Channels, https://psx-spx.consoledev.net/ps1/system/dmachannels/ (DICR)"
  verification: "Inferred from the SCPH-1001 kernel's behavior on the host (core-log entry 27): with a level-triggered bit 3, its DMA handler re-entered on every acknowledge; with edge triggering the disc boot proceeds."

- record_id: PSXIO-004
  kind: PROCESSOR
  topic_id: PSXIO
  title: "Controller-port digital pad protocol"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "The controller and memory-card port (SIO0) is at 1F801040h (TX/RX data), 1F801044h (JOY_STAT: bit 0 TX ready, bit 1 RX not empty, bit 2 TX finished, bit 7 /ACK level, bit 9 interrupt), 1F801048h (mode), 1F80104Ah (JOY_CTRL: bit 1 select, bit 4 acknowledge, bit 6 reset, bit 12 /ACK interrupt enable, bit 13 port 2), and 1F80104Eh (baud). A digital pad answers the host bytes 01 42 00 00 00 with FF 41 5A, then the low and high button bytes (active low), and pulls /ACK after every byte except the last; /ACK raises IRQ7 when enabled. An empty port or memory-card address 81h answers FF without /ACK."
  consequence: "software/psx/sio.c models a digital pad with no buttons pressed in port 1 and empty memory-card slots. The SCPH-1001 shell polls the port after its intro and hangs without it."
  sources:
    - "PSX-SPX, Controllers and Memory Cards, https://psx-spx.consoledev.net/ps1/controllersandmemorycards/"
  verification: "Inferred: the SCPH-1001 BIOS's pad driver completes its polls against this model on the host and the AE350 (core-log entry 27). Memory-card and analog-pad protocols are not modeled."
- record_id: TOOL-006
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "LiteDRAM Wishbone burst frontend timing"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "When the Wishbone data width is narrower than the native port, LiteDRAMWishbone2Native (litedram commit c454a44, frontend/wishbone.py, _init_burst_upconverter) decides write merging (wr_can_merge) and read-cache hits by comparing the full native address combinationally in its CMD state. Those decisions drive its Wishbone ack and the enable of its 256-bit write buffer. Behind the AE350's bursting AHB bridge on GW5AST at 75 MHz, those paths were the critical setup failures, both directly into the AE350 macro and into the write buffer."
  consequence: "Tang-PSX uses BurstWishbone2Native (gateware/ae350_ram_bridge.py), which decides burst continuation from the burst type and previous lane instead of address compares."
  sources:
    - "LiteDRAM commit c454a44 (third_party/litedram), litedram/frontend/wishbone.py"
  verification: "Timing paths in core-log entries 17 and 19; replacing the frontend moved three of four placement variants from failing to passing."

- record_id: TOOL-007
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "Gowin place-and-route routing and replication options"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "SUG100-4.4.2E section 8.3 documents the project options set_option -route_option <0|1|2> (0 the default routing algorithm, 1 and 2 alternative routing algorithms) and set_option -replicate_resources <0|1> (1 replicates high-fanout resources to reduce fanout and improve timing), alongside -place_option <0|1|2|3|4> (0 the default placement algorithm). Gowin EDA 1.9.11.03 gw_sh accepts all three in run.tcl."
  consequence: "gateware/ae350_gate1.py passes --place-option and --route-option into the LiteX Gowin toolchain options, which emits them as set_option lines; resource replication is not enabled."
  sources:
    - "Gowin Software User Guide SUG100-4.4.2E, section 8.3 Command Description, shipped as IDE/doc/EN/SUG100-4.4.2E_Gowin Software User Guide.pdf in Gowin EDA 1.9.11.03 (SHA-256 708f510811c6deb9320da118c7f04f656da41a54a62291cf8079ffcb40870f83)"
  verification: "On 2026-09-29 (core-log entry 29) the same Gate 1 netlist at placement 3 routed to different timing with route options 0, 1, and 2, and enabling replicate_resources changed every placement's result."

- record_id: TOOL-008
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "GNU Lightning RISC-V backend word size and register-pair order"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "At commit a6bb2b5a7cf3 the GNU Lightning RISC-V backend is 64-bit only: lib/jit_riscv.c ends with #error \"only 64 bit ports tested\" for __WORDSIZE != 64, and lib/jit_riscv-sz.c has only a __WORDSIZE == 64 size table. Lightrec (a7464ccc65c5) emits all code through GNU Lightning. doc/body.texi defines movr_ww_d, movi_ww_d, movr_d_ww, and movi_d_ww (32-bit only) with the integer pair in memory order, so on little-endian targets the first register holds the low word."
  consequence: "Lightrec cannot target the RV32 AE350 without an RV32 Lightning backend. Tang-PSX adds one in third_party/patches/gnu-lightning-rv32.patch, applied by tools/lightning_source.py and tested by tests/test_lightning_rv32.py; its lib/jit_fallback.c change corrects the little-endian unldi_x/unsti_x pair order to this definition."
  sources:
    - "GNU Lightning via notaz/gnu_lightning commit a6bb2b5a7cf36e074e12ccaed32990b437deb784: lib/jit_riscv.c, lib/jit_riscv-sz.c, lib/jit_fallback.c, doc/body.texi"
    - "Lightrec commit a7464ccc65c5360897ff6814d9707968f34a1d2b, lightning-wrapper.h"
  verification: "The unpatched RV32 build stops at the #error; the patched check suite passes under qemu-riscv32 (core-log entry 31). The canonical git.savannah.gnu.org repository could not be reached on 2026-09-29, so a newer upstream RV32 port is not excluded."

- record_id: TOOL-009
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "GCC ilp32d passing of double arguments"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "With -march=rv32imafdc -mabi=ilp32d, GCC passes a named double in fa0-fa7 while one is free, then as an integer register pair (low word in the lower-numbered register). When only a7 remains, the low word goes in a7 and the high word in the first stack slot. Stack doubles are 8-byte aligned (an int at sp+0 is followed by a double at sp+8). Variadic doubles use an even-aligned register pair (a1 is skipped after a0) or, when only a7 remains, an 8-byte aligned stack slot without splitting."
  consequence: "The RV32 Lightning backend in TOOL-008 follows these rules for arg_d, getarg_d, putarg*_d, pusharg*_d, and va_arg_d. Code built with -mabi=ilp32, as the AE350 programs are, instead passes named floating-point arguments in integer registers; the backend does not implement that, which Lightrec does not need."
  sources:
    - "riscv64-unknown-elf-gcc 10.2.0 (Xuantie-900 elf newlib Toolchain V2.6.1 B-20220906) assembly output for calls with 8 doubles plus 7 or 9 ints plus a double, and printf with an int and a double"
  verification: "Read from the compiler's generated assembly on 2026-09-29, and confirmed when Lightning's ccall, carg, and cva_list checks passed against GCC-compiled C under qemu-riscv32 with ilp32d (core-log entry 31). The RISC-V ELF psABI document itself was not consulted."

- record_id: TOOL-010
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "Lightrec host contract for interrupts, GTE, cache isolation, and cycles"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "At Lightrec commit a7464ccc65c5 (upstream HEAD on 2026-09-29): lightrec_set_exit_flags sets target_cycle to current_cycle, so compiled code leaves at the end of the current block, not after the calling instruction. Interrupts are therefore expected on block boundaries, never right after a GTE command; interpreter.c int_delay_slot moves a JR/JALR whose delay slot is RFE back by one instruction when EPC holds a COP2 opcode and the return address is EPC + 4, so the GTE command then runs once, and the optimizer (lightrec_detect_impossible_branches) always interprets such branches. emitter.c rec_mtc0 calls the enable_ram callback for Status bit 16 only when the block does not run from RAM through kuseg or kseg0 (block_uses_icache); code there is assumed never to isolate the cache. optimizer.c converts a JR to a known target into a J when the kunseg addresses share their top four bits, and J keeps the block's own segment, so a known jump from kseg0 RAM into kseg1 stays in kseg0. emitter.c rec_special_SYSCALL notes as a TODO that a SYSCALL in a delay slot is not handled. The cycle counter and target are 32-bit, lightrec_execute replaces a target below the current count with UINT_MAX, and the block cache ages blocks by the wrapped difference of the counter."
  consequence: "software/lightrec/psx_lightrec.c does not execute the GTE command on interrupt entry, takes an interrupt raised in a block before a SYSCALL or BREAK that ends it, keeps Lightrec's counter to the low 31 bits of the machine count, and documents the cache-isolation and JR-to-J limits; tests/psx_lightrec_unit_rv32.c reaches kseg1 through a loaded address."
  sources:
    - "Lightrec commit a7464ccc65c5360897ff6814d9707968f34a1d2b (third_party/lightrec): lightrec.c lightrec_set_exit_flags, lightrec_execute, lightrec_mtc0; interpreter.c int_delay_slot; emitter.c rec_mtc0, block_uses_icache, rec_special_SYSCALL; optimizer.c lightrec_transform_ops (Convert JR to J), lightrec_detect_impossible_branches; blockcache.c lightrec_block_is_old"
  verification: "tests/psx_lightrec_unit_rv32.c under qemu-riscv32 and built natively for x86-64 showed the GTE command running twice when the host also executed it on interrupt entry, an isolated store reaching RAM from kseg0 code, and a constant JR to kseg1 continuing at the kseg0 address (core-log entry 33)."

- record_id: TOOL-011
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "git apply inside a work tree ignores paths outside the current directory"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "git-apply(1), Git 2.53.0, DESCRIPTION: when running from a subdirectory in a repository, patched paths outside the directory are ignored. The paths in a patch are taken relative to the top of the work tree that contains the current directory, so applying a patch to a tree exported below another repository's work tree (for example under an ignored build/ directory) skips every file and still exits with status 0."
  consequence: "tools/lightning_source.py applies gnu-lightning-rv32.patch with GIT_CEILING_DIRECTORIES set to the destination's parent, so Git does not discover the Tang-PSX work tree and the patch applies to the exported GNU Lightning tree wherever it is placed."
  sources:
    - "git-apply(1) manual page, Git 2.53.0 (git help -m apply), DESCRIPTION"
  verification: "Building liblightrec.a under build/lightrec produced an unpatched GNU Lightning (jit_riscv-sz.c without the RV32 table, JIT_INSTR_MAX undefined) while the same build under /tmp was patched; with the ceiling set, the build under build/ is patched and the Lightning check suite passes (core-log entry 36)."

- record_id: TOOL-012
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "GNU Lightning RISC-V compare-immediate and branch-range defects"
  status: VERIFIED
  verified_date: 2026-09-29
  statement: "In lib/jit_riscv-cpu.c of both notaz/gnu_lightning a6bb2b5 and the canonical GNU Lightning master 7965700 (git.savannah.gnu.org, 2026-08-25), _lti and _lti_u with an immediate outside simm12, and _gti and _gti_u always, execute movi(r0, i0) and then compare r1 with the temporary t0, which is never loaded, so the result is undefined. Conditional branches are one B-type instruction whose offset _Btype and _patch_at require to satisfy simm12_p (+-2 KiB, half the encoding's range), and a forward branch is emitted before its target is known, so a target patched more than 2 KiB away fails the assertion. Master 7965700 still ends lib/jit_riscv.c with #error \"only 64 bit ports tested\" for __WORDSIZE != 64."
  consequence: "third_party/patches/gnu-lightning-rv32.patch loads the immediate into the temporary, and its _bcc emits a single B-type branch only for a known target in range, otherwise the inverted branch over a JAL that _patch_at retargets up to +-1 MiB; both size tables allow for the longer branches. No newer upstream RV32 backend exists to replace the Tang-PSX port. Both defects also affect RV64 hosts."
  sources:
    - "GNU Lightning master 7965700 (https://git.savannah.gnu.org/git/lightning.git, 2026-08-25), lib/jit_riscv-cpu.c _lti, _lti_u, _gti, _gti_u, _Btype, _patch_at; lib/jit_riscv.c"
    - "notaz/gnu_lightning a6bb2b5a7cf36e074e12ccaed32990b437deb784, same functions"
  verification: "The SCPH-1001 shell's sltiu at, a0, 0x1000 at 0x80055fb0 took the wrong branch under compiled RV32 Lightrec, so the BIOS's EXE header read failed and Spyro stopped at the PlayStation logo; with the compare fixed, a Lightrec block aborted in _Btype 17 emulated seconds into Spyro. With both fixes Spyro under qemu-riscv32 matches native x86-64 Lightrec for 60 emulated seconds and boots to its main menu on the AE350 (core-log entry 38)."

- record_id: TOOL-001
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "Headless Gowin IP generation with gw_sh"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "In gw_sh (1.9.11.03), after create_project -pn <part> -device_version <rev>: `create_ipc -name ddr3 -dir <D> -module_name <M> -file_name <F> -language Verilog` creates the IP in <D>/<F>/; the IP name is the ipspec id (ddr3), and -language accepts Verilog. `set_property -dict {CONFIG.<Name> <value> ...} [get_ips <M>]` takes human values (for example Memory_Clock 400, Dq_Width 32, CAS_Latency 6), and the emitted .ipc indices match the IDE's. `generate_target [get_ips <M>]` synthesizes the encrypted IP. `read_ipc` segfaults in batch mode even on a .ipc in the tool's own format, and relative paths in these commands resolve against the project directory. `report_property [get_ips <M>]` lists every CONFIG.* property."
  consequence: "scripts/gen-ddr3-ip.sh regenerates the DDR3 IP deterministically and checks the emitted .ipc against the committed reference."
  sources:
    - "Gowin EDA 1.9.11.03 gw_sh built-in help (create_ipc, set_property, generate_target, read_ipc, report_property, write_ip_tcl)"
  verification: "Exercised on 2026-09-28; the regenerated IP is hardware-proven (core-log entries 16 and 17)."

- record_id: TOOL-002
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "PLL regeneration with GowinModGen"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "`GowinModGen -do <file>.mod` regenerates a primitive-based IP such as PLL_ADV from its .mod file into the directory named by the file's -path line. PLL_INIT, which dynamically searches ICP/LPF settings for PLLs built with dyn_icp_sel/dyn_lpf_sel, ships as IDE/ipcore/PLL_ADV/data/PLL/pll_init.v."
  consequence: "The DDR3 PLL wrapper and PLL_INIT are regenerated or copied from the Gowin install at build time and are not committed."
  sources:
    - "Gowin EDA 1.9.11.03 installation (bin/GowinModGen, IDE/ipcore/PLL_ADV)"
  verification: "Regenerated gowin_pll_mod.v matched Sipeed's apart from its header and one blank line; hardware-proven."

- record_id: TOOL-003
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "Gowin SDC clocks must name surviving nets"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "Gowin timing analysis rejects create_clock on a net that synthesis merged away (ERROR TA2003 'Can't set timing constraint to object'), and every later constraint naming that clock fails with TA2004. When a LiteX clock-domain signal is only an alias of a PLL output, constrain the PLL's output net instead (LiteX GW5APLL clkouts[n].clk, emitted as main_clkout)."
  consequence: "ae350_gate1.py constrains the system clock with a generated-clock constraint on the GW5APLL output net named sys_clk."
  sources:
    - "Gowin EDA 1.9.11.03 timing-analysis messages TA2003/TA2004"
  verification: "Observed and fixed on 2026-09-28 (core-log entry 17)."

- record_id: TOOL-004
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "LiteX registers the CSR bridge only with LiteDRAM present"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "LiteX SoC._finalize_bus() calls add_csr_bridge(with_register=hasattr(self, 'sdram')). A SoC with no LiteDRAM sdram core therefore gets an unregistered Wishbone2CSR bridge, which puts CSR decoding in the same cycle as the bus master's address."
  consequence: "Gate1SoC overrides add_csr_bridge to force with_register=True; without it every placement option failed 75 MHz sys_clk timing."
  sources:
    - "LiteX commit 9fa53dc19a22, litex/soc/integration/soc.py (_finalize_bus, add_csr_bridge)"
  verification: "Timing failure and fix observed on 2026-09-28 (core-log entry 17)."

- record_id: TOOL-005
  kind: TOOLCHAIN
  topic_id: TOOL
  title: "LiteX CSRStatus with fields is driven through its fields"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "CSRStatus(fields=[...]) combinationally assigns each status bit slice from its field signal. Also assigning the whole status signal leaves the fields undriven; Gowin then warns EX3780 ('Using initial value ... since it is never assigned') and the CPU reads zeros."
  consequence: "Drive csr.fields.<name>, never csr.status, for field-based CSRStatus registers."
  sources:
    - "LiteX commit 9fa53dc19a22, litex/soc/interconnect/csr.py (CSRStatus.__init__)"
  verification: "The ddr3 status CSR read 0 on hardware until the fields were driven (core-log entry 17)."

- record_id: TCTL-001
  kind: EXTERNAL
  topic_id: TCTL
  title: "Tang-Control core upload and loading"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "`scripts/tangctl.py put <local> <remote>` uploads to the SD card with size and CRC32 readback verification. The firmware refuses uploads ('ERR return to the TangCore main menu first') while a core is running. Cores are searched in cores/<board>/ and then cores/ on sd: or usb:; the 138K board name is console138k. Arbitrary core files are loaded from the main menu's Cores entry."
  consequence: "Deployment is: the user returns to the main menu, the agent uploads to cores/console138k/<name>.bin, and the user loads it from Cores."
  sources:
    - "Tang-Control commit 26e975bef22b (branch feature/usb-cdc-file-transfer): scripts/tangctl.py, core/cores.cpp, main.cpp, usb/usb_cdc_console.cpp"
  verification: "Used for every deployment on 2026-09-28."

- record_id: TCTL-002
  kind: EXTERNAL
  topic_id: TCTL
  title: "FPGA debug-register transport"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "`tangctl.py peek <addr> [count<=64]` and `poke <addr> <value>` issue FPGA_EXT_READ32 (0x01) and FPGA_EXT_WRITE32 (0x02) requests. The FPGA side is Tang-Phosphor's iosys_bl616, which exposes debug_valid, debug_write, debug_address, debug_wdata, and debug_rdata and counts CRC and malformed-request errors. The UART runs at 2,000,000 baud (with a 5,000,000 baud fast mode via EXT_SET_BAUD), and the iosys clock FREQ must be at least 8x the baud rate. The legacy core-ID response carries only the low byte of CORE_ID. `tangctl.py status` reports transport counters (fpga_requests, fpga_responses, timeouts, CRC, malformed, unexpected, FIFO overflows)."
  consequence: "Diagnostic images expose a 32-bit register file on the debug bus; register the read mux (the address is stable several UART bytes before sampling)."
  sources:
    - "Tang-Control commit 26e975bef22b: utils/fpga_debug.h, scripts/tangctl.py"
    - "Tang-Phosphor commit 292ae779da23: src/iosys/iosys_bl616.v, src/iosys/uart_fixed.v"
  verification: "Used at 50 MHz (DDR3 test) and 75 MHz (Gate 1) iosys clocks with zero transport errors."

- record_id: TCTL-003
  kind: EXTERNAL
  topic_id: TCTL
  title: "Loaded-core indication in tangctl status"
  status: INFERRED
  verified_date: 2026-09-28
  statement: "With the Gate 1 core loaded and serving debug requests, `tangctl.py status` reported `active_core: 81` (0x51, the low byte of Gate 1's core ID) together with `core_running: no`. At the TangCore main menu it reported `active_core: 0`."
  consequence: "Wait for a core with active_core, not core_running. Uploads need active_core 0 (TCTL-001)."
  sources:
    - "Tang-Control commit 26e975bef22b, scripts/tangctl.py status output"
  verification: "Observed repeatedly on 2026-09-28 (core-log entry 26) while Gate 1 answered peek requests."

- record_id: TCTL-004
  kind: EXTERNAL
  topic_id: TCTL
  title: "Tang-PSX disc service"
  status: VERIFIED
  verified_date: 2026-09-28
  statement: "While active_core is 81 and debug address 0x04 reads 0x00020002 or later, Tang-Control's core/tangpsx.cpp finds the first .cue in the SD root, writes the size of its FILE's .bin in 2352-byte sectors to debug address 0x20c, and polls 0x200. Each new sequence value is served when a second read of 0x200 after reading offset 0x204 and length 0x208 matches: one FPGA stream session (START, DATA, END with the byte count) carrying that byte range, at most 256 sectors. fpga_file_stream switches the link to 5,000,000 baud for each session. The USB console status reports psx_disc, psx_disc_sectors, psx_disc_published, psx_disc_requests, psx_disc_failed, and psx_disc_bytes."
  consequence: "Gate 1 loader API 2 (disc_request, stream_read, disc_sectors) is the firmware side of this contract; software/programs/psx_disc requests 32-sector windows with one outstanding request."
  sources:
    - "Tang-Control commit fbbddc61060a (feature/usb-cdc-file-transfer), core/tangpsx.cpp and utils/fpga_file_stream.cpp"
  verification: "On 2026-09-28 (core-log entry 27) it served 23 requests (1,731,072 bytes) of a 281,270-sector image to psx_disc with no failures, cancels, or FIFO overflow."
```

---

## 6. Record template

```yaml
- record_id: <TOPIC>-<NNN>
  kind: DEVICE | BOARD | MEMORY | PROCESSOR | TOOLCHAIN | EXTERNAL
  topic_id: <TOPIC>
  title: "Short noun phrase"
  status: VERIFIED | SOURCED | INFERRED | SUPERSEDED
  verified_date: YYYY-MM-DD
  statement: "What the source says, with exact values and units"
  consequence: "What it means for this project, or where it is implemented"
  sources:
    - "Document or repository, revision/commit/SHA-256, section or path, URL"
  verification: "How and when it was confirmed, or why it is not yet confirmed"
  superseded_by: "<record_id>"   # only on a SUPERSEDED record
```

---

## 7. Maintenance boundary

- Keep external facts, their sources, and their verification status here.
- Keep build results, failures, rationale, and chronology in `core-log.md`; cite a log entry number in `verification` instead of repeating it.
- Do not add speculative records for topics that have not been looked up. An unanswered question stays out of this file until a source answers it.
- When a record's source is superseded by a newer document or tool version, add a new record rather than editing the old statement.
- A `core-syntax.md` audit is required whenever this file changes, per `.ai/core.md`.

```yaml
last_reviewed: 2026-09-29
```
