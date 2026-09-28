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
| What DDR3 controller configuration is proven on this board? | DDR3 | DDR3-001 |
| How do I drive the Gowin DDR3 native (user) port correctly? | DDR3 | DDR3-002 |
| How is the native-port address laid out? | DDR3 | DDR3-003 |
| Which Gowin DDR3 IP version does our toolchain ship, and what differs? | DDR3 | DDR3-004 |
| What is on the SDRAM add-on and which connector pins does it use? | SDR | SDR-001 |
| Where does the AE350 reset, and where are its bus windows? | AE350 | AE350-001 |
| How do I enable, inspect, and flush the AE350 caches? | AE350 | AE350-002 |
| Which AE350 address ranges are cached? Is the data cache write-back? | AE350 | AE350-003 |
| What cache geometry does this board's AE350 report? Does fence.i cover the D-cache? | AE350 | AE350-004 |
| How do I regenerate Gowin IP without the GUI? | TOOL | TOOL-001, TOOL-002 |
| Why does a Gowin SDC clock fail to attach to a net? | TOOL | TOOL-003 |
| Why did CSR timing change after removing LiteDRAM? | TOOL | TOOL-004 |
| How must a LiteX CSRStatus with fields be driven? | TOOL | TOOL-005 |
| How do I upload a core and read FPGA registers through Tang-Control? | TCTL | TCTL-001, TCTL-002 |

---

## 4. Fast lookup index

```yaml
DEV-001: "Revision is the 5th character of the package's second marking line; this board (2518CA0N) is revision C"
DEV-002: "GW5AST-138: 138,240 LUTs, 340 BSRAM blocks of 18 Kbit"
BRD-001: "Two Hynix H5TQ4G63EFR-RDC x16 DDR3 devices form a 32-bit bus: DQ[31:0], DQS[3:0], DM[3:0], 1 GiB"
BRD-002: "x32 DDR3 pin map for PG484, SSTL15 at 1.5 V, from Sipeed's constraints"
BRD-003: "50 MHz oscillator on V22; BL616 UART to FPGA on V14 (FPGA RX) and U15 (FPGA TX), LVCMOS33"
DDR3-001: "Proven controller: 400 MHz memory clock, 1:4, CL6/CWL5, RTT_NOM 40, 256-bit native port on a 100 MHz user clock"
DDR3-002: "Native port: cmd 000 write / 001 read; command and its write data in the same cycle; wr_data_mask 1 = byte skipped; read data cannot be back-pressured"
DDR3-003: "addr[28:0] is in 32-bit words, one BL8 burst = 8 words; addr[28] (rank) is unused; addr[27:0] spans 1 GiB"
DDR3-004: "Gowin EDA 1.9.11.03 ships DDR3 IP 5.9; Sipeed's 6.0 adds AXI/arbitration/MC-BSRAM options; WRITE_RECOVERY is ignored at this configuration"
SDR-001: "Add-on SDRAM is Winbond W9825G6KH-6: 32 MB x16 per chip, 166 MHz grade, on dock connectors J9/J10"
AE350-001: "AE350 reset vector is fixed at 0x80000000; CPU-master extended AHB window at 0xE8000000 (0x08000000 long)"
AE350-002: "Caches reset disabled; mcache_ctl 0x7CA bit0 IC_EN, bit1 DC_EN; mcctlcommand 0x7CC value 6 = L1D write-back+invalidate all; micm/mdcm/mmsc_cfg at 0xFC0/0xFC1/0xFC2"
AE350-003: "The A25 L1 data cache is write-back; the 0xE8000000-0xEFFFFFFF peripheral window is uncached"
AE350-004: "This board's A25: micm_cfg = mdcm_cfg = 0x00439ADA (32 KiB 4-way, 32 B lines, inferred), mmsc_cfg = 0x2007F039; fence.i makes D-cache stores visible to instruction fetch"
TOOL-001: "gw_sh create_ipc/set_property/generate_target regenerate IP headlessly; read_ipc segfaults in batch mode"
TOOL-002: "GowinModGen -do <file>.mod regenerates PLL wrappers; PLL_INIT ships in IDE/ipcore/PLL_ADV/data/PLL/pll_init.v"
TOOL-003: "Gowin SDC cannot attach a clock to a net merged away by synthesis (TA2003); constrain the surviving PLL output net"
TOOL-004: "LiteX registers its Wishbone-to-CSR bridge only when the SoC has a LiteDRAM sdram core"
TOOL-005: "LiteX CSRStatus(fields=...) drives status from its field signals; drive the fields, not status"
TCTL-001: "Uploads (put) are refused unless the TangCore main menu is active; cores load from cores/console138k/"
TCTL-002: "peek/poke use FPGA_EXT_READ32/WRITE32 over iosys_bl616 at 2,000,000 baud; core ID is reported by its low byte"
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
last_reviewed: 2026-09-28
```
