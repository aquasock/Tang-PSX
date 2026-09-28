## 1 COMMIT Unreleased 2026-09-28T01:58:23-07:00

#### Coming From:

None.

#### Purpose:

Bring up the headless Gate 1 AE350 diagnostic image on hardware and determine whether it boots, reports through Tang-Control, and verifies board DDR3 at the 75 MHz controller clock.

#### Outcome:

The FPGA package is marked `GW5AST-LV138PG484AC1/I0`, `2518CAON`, `TS0E44.00`; per Sipeed's Gowin marking rule the fifth character of the lot/date line identifies device revision C, so revision C is the hardware target and the revision-B entry in `.ai/core.md` is stale. Tang-Control loaded core ID `0x51` and returned diagnostic magic `0x54505831` (`TPX1`) over the FPGA UART transport with no transport CRC or malformed-request errors. Bare-metal firmware executed from the AE350's fixed `0x80000000` reset vector and wrote the diagnostic CSRs, and clock/reset status `0x00000011` proved the independent AE350 PLL and the main DDR/system PLL locked while the system, DDR PHY, and CPU resets were released. Artifact `faf55209890883ee03f64c721d9874763da0d682e3663c54925c4c7f759e564c` used the direct AE350/LiteDRAM bridge; SDRAM initialization returned, but deterministic DDR verification failed at word 0 with expected `0x510c4619` and observed `0x5100267a`, the exact word-1 pattern. Artifact `fb10611a5a47cd6f441593dc1ccd507c5a234bd39159d905a9cfc9d33ad30652` used the conservative fabric Wishbone path; initialization again returned, but verification failed at word 0 with observed `0x287a8da2`. Gate 1 is not complete because no deterministic DDR pass or runtime DDR code execution has been demonstrated.

#### Next Steps:

Isolate the word-0 DDR verification failure so that deterministic DDR readback and runtime DDR code execution can be demonstrated, and ask the user whether to correct the stale revision-B device entry in `.ai/core.md` to revision C.

#### Files Modified:

- .gitignore
- .gitmodules
- README.md
- gateware/ae350_gate1.py
- scripts/build-gate1.sh
- scripts/env.sh
- software/gate1/Makefile
- software/gate1/crt0.S
- software/gate1/linker.ld
- software/gate1/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 2 COMMIT Unreleased 2026-09-28T02:07:43-07:00

#### Coming From:

None.

#### Purpose:

Determine whether Gate 1 passes on revision-C hardware when the fabric and DDR controller clock is reduced to 50 MHz.

#### Outcome:

Revision-C artifact `48cd7e3fceaaeb7669a02835f6714315a60c96c03a7238ed70376a269bed00cc` kept the AE350 at 750 MHz, used the conservative fabric Wishbone RAM path, and reduced the fabric/controller clock to 50 MHz (100 MHz DDR CK, DLL-off PHY mode). Tang-Control diagnostics reported terminal stage `0x80000001`, no failure, 16,384 verified DDR words, checksum `0x63c6e40c`, feature bitmap `0x7`, and zero transport CRC or malformed-request errors. Runtime-written RV32 code in DDR returned 42, was replaced, and returned 99 after `fence rw,rw` plus `fence.i`, giving packed diagnostic result `0x002a0063`. Clock/reset status remained `0x00000011` with both PLLs locked and all relevant resets released, and the measured firmware interval was `0x1813843c` AE350 cycles. Gate 1 requirements 1 through 4 are therefore hardware-proven at the diagnostic 50 MHz fabric clock by the device-side diagnostic; no explicit user acceptance of this result was recorded. The required 75 MHz configuration remains unresolved, since both earlier 75 MHz PHY runs failed deterministic DDR readback at word 0.

#### Next Steps:

Restore the required 75 MHz fabric and DDR controller clock and diagnose why deterministic DDR readback fails at word 0 in that configuration.

#### Files Modified:

- gateware/ae350_gate1.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: NOT RUN

---

## 3 COMMIT Unreleased 2026-09-28T03:08:31-07:00

#### Coming From:

None.

#### Purpose:

Diagnose the 75 MHz DDR failure by running quarter-rate PHY calibration with a firmware log and passive PHY-operation counters on reliably powered hardware.

#### Outcome:

Revision-C artifact `358e20223babbf9ef9fd4514eb8b4927e1d73f159508939fb4998d59d4e41209` used the AE350 at 750 MHz, the conservative fabric Wishbone path, a 75 MHz controller clock, and the GW5 DDR PHY in 1:4 mode with a 300 MHz fast clock; its diagnostic ABI 1.2 adds a 128-byte firmware log and passive PHY-operation counters. Gowin reported zero same-clock TNS for every named clock, with diagnostic, system, and fast-clock Fmax of 78.191, 175.505, and 848.356 MHz. The first run was excluded because the user identified that normal board power had not been connected reliably, and the identical artifact was then run from a clean reset with normal power and the Tang-Control data cable both continuously connected. The powered run completed the exhaustive write-DQ/DQS bootstrap and final read-leveling sequence, but both DDR byte lanes printed only failing scan points and `delays: -`; the final log selected byte lane 1 bitslip 4 as the least-bad result without any zero-error delay window. Calibration nevertheless returned success and handed the DFI bus to the controller, after which the deterministic test failed at word 0 with expected `0x510c4619`, observed `0xf7fbffff`, terminal stage `0x80010002`, failure `0x00010002`, and feature bitmap `0x1` (initialization only). Clock/reset status remained `0x00000011`, and Tang-Control matched all 229 FPGA requests with responses and reported zero drops, timeouts, CRC errors, malformed packets, unexpected responses, or receive-FIFO overflows.

#### Next Steps:

Build a diagnostic that separates missing read bursts from incorrectly sampled data at forced tap and bitslip points, and use it to verify the GW5 quarter-rate burst capture, DQS delay direction and reset behavior, and DFI phase alignment before changing calibration-search speed.

#### Files Modified:

- gateware/ae350_gate1.py
- software/gate1/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 4 COMMIT Unreleased 2026-09-28T10:06:09-07:00

#### Coming From:

None.

#### Purpose:

Determine whether both DDR byte lanes produce read bursts at 75 MHz in X4 mode using a bounded direct-DFI read-burst scan.

#### Outcome:

Revision-C artifact `754b99396654ee38de1423d8985a2a4b6a12429982cea8dd2035296e438074a1` used the AE350 at 750 MHz, a 75 MHz controller clock, the GW5 DDR PHY in 1:4 mode, diagnostic ABI 1.3, and a bounded direct-DFI read-burst scan; the image was 4,539,900 bytes and its upload/readback CRC32 was `0x7e4cf84a`. The build used Gowin placement option 3 and met every named clock with zero setup and hold TNS, with reported Fmax of 81.369 MHz for the 75.002 MHz diagnostic clock, 187.980 MHz for the 75 MHz system clock, and 520.748 MHz for the 300 MHz fast DDR clock. A registered software DFI command path and a Gate-1-local AE350 peripheral AHB bridge removed the previously failing CSR-to-DDR-serializer and AHB-FSM-to-Wishbone-CE paths. After JEDEC initialization, firmware scanned both byte lanes through all 8 input bitslips and all 256 read-delay taps, issuing 4,096 direct DFI reads, and both lanes observed read bursts at every bitslip (packed mask `0x0000ffff`) with 695 detections on lane 0 and 738 on lane 1 (`0x02e202b7`), the first detection for each lane being at bitslip 0, delay 0. The diagnostic reached terminal stage `0x800000d1` with failure 0 and feature bitmap `0x7` (JEDEC initialization plus burst detection on both lanes); passive counters reported 8,192 read-delay operations, zero write-delay operations, and 4,103 software DFI commands. Raw DFI words at the first detected burst were `0xffffffdf`, `0xffffffdf`, `0xffefffff`, and `0xffffffef`, but the scan intentionally wrote no known pattern, so these values are not a data-integrity verdict. Clock/reset status remained `0x00000011` and the Tang-Control status sample reported no drops, timeouts, CRC errors, malformed packets, unexpected responses, or receive-FIFO overflows. Missing DQS/read-burst activity is therefore excluded as the cause of the earlier 75 MHz failure; the device-side diagnostic passed, and no explicit user acceptance was recorded.

#### Next Steps:

Investigate the remaining boundary of the write-data/DQS path and read sample/data-phase alignment by writing a known pattern through direct DFI commands and scanning for correct data, preserving the proven read-burst instrumentation.

#### Files Modified:

- gateware/ae350_gate1.py
- software/gate1/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: NOT RUN

---

## 5 COMMIT Unreleased 2026-09-28T10:45:09-07:00

#### Coming From:

None.

#### Purpose:

Correct per-phase write capture in the registered DFI injector and test whether a known four-phase write pattern reads back correctly at the default write delay.

#### Outcome:

Revision-C artifact `382b5718d6f866698067d4bc2e64caf2df41e4d61c43d776c6a67b479815f6f4` used diagnostic ABI 1.4 and corrected the registered DFI injector so each phase captures its independently programmed write payload before the single write command is issued; its size was 4,539,900 bytes and Tang-Control verified upload/readback CRC32 `0xd368d61b`. Gowin placement option 3 met every named clock with zero setup and hold TNS, with reported Fmax of 79.045 MHz for the 75.002 MHz diagnostic clock, 165.280 MHz for the 75 MHz system clock, and 638.977 MHz for the 300 MHz fast DDR clock, while parallel builds with placement options 1, 2, and 4 failed timing and were excluded from hardware testing. Firmware initialized DDR3, issued one direct-DFI BL8 write containing four distinct 32-bit phase patterns at the reset/default write delay, then scanned every read bitslip and read-delay coordinate, reaching terminal stage `0x800000d2` with bursts detected on both lanes at all eight bitslips (660 detections on lane 0 and 719 on lane 1). No exact-data coordinate existed at that fixed write delay: failure was `0x00040003`, exact-match masks and counts were zero, and the closest samples had 26 bit errors on lane 0 at bitslip 0/read delay 78 and 24 bit errors on lane 1 at bitslip 0/read delay 84. The captured phase words contained stable portions of the programmed pattern rather than the earlier near-all-ones data, proving that four-phase write payload now reaches the DDR interface but that the reset/default write-DQ/DQS delay is outside the complete-data window. Clock/reset status remained `0x00000011`, and Tang-Control matched all 35 diagnostic requests with responses and reported zero drops, timeouts, CRC errors, malformed packets, unexpected responses, or receive-FIFO overflows. The corrected four-phase write capture removes the defect that invalidated the earlier full calibration search.

#### Next Steps:

Rerun the normal full Gate 1 calibration, deterministic DDR test, and runtime-code execution at 75 MHz, relying on the write-DQ/DQS training that already sweeps write delay when initial reads fail.

#### Files Modified:

- gateware/ae350_gate1.py
- scripts/build-gate1.sh
- software/gate1/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 6 COMMIT Unreleased 2026-09-28T11:01:28-07:00

#### Coming From:

None.

#### Purpose:

Rerun the complete 75 MHz X4 Gate 1 calibration, deterministic DDR test, and runtime-code probe with corrected four-phase DFI write capture.

#### Outcome:

Revision-C artifact `837399e1f4921dae49f7bea9d368d2c712d61401be5b03339a754b8d1358a8e9` used diagnostic ABI 1.5 and reran the complete 75 MHz/X4 Gate 1 flow with corrected four-phase DFI write capture; its size was 4,524,028 bytes and Tang-Control verified upload/readback CRC32 `0x2f3e35c8`. Gowin placement option 3 again met every named clock with zero setup and hold TNS, with reported Fmax of 82.365 MHz for the 75.002 MHz diagnostic clock, 159.037 MHz for the 75 MHz system clock, and 637.577 MHz for the 300 MHz fast DDR clock, while parallel placement options 1, 2, and 4 failed timing and were excluded from hardware testing. Write-DQ/DQS bootstrap exhaustively searched the complete delay range on both byte lanes, and both lane logs ended with all-zero pass maps and `delays: -`; final read leveling also found no valid window. Passive counters reached 322,709 read-delay operations, 2,044 write-delay operations, and 1,891,149 direct DFI commands before calibration returned control to the hardware controller. The deterministic DDR test then failed immediately at word 0 with expected `0x510c4619` and observed `0xffffffff`, terminal stage `0x80010002`, failure `0x00010002`, and feature bitmap `0x1`, recording initialization only, so DDR verification and runtime DDR code execution were not reached. Clock/reset status remained `0x00000011`, and Tang-Control matched all 252 FPGA requests with responses and reported zero drops, timeouts, CRC errors, malformed packets, unexpected responses, or receive-FIFO overflows. Exhaustive read and write tap selection is now excluded as the remaining cause.

#### Next Steps:

Investigate X4 write serializer ordering, the DQS-to-DQ phase relationship, and command/data cycle alignment, which together with the preceding diagnostic's stable partial pattern at the default write delay form the next failure boundary.

#### Files Modified:

- gateware/ae350_gate1.py
- software/gate1/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 7 COMMIT Unreleased 2026-09-28T11:09:55-07:00

#### Coming From:

None.

#### Purpose:

Separate temporal serializer ordering from basic DQ drive and read capture by writing complementary phase-invariant BL8 patterns to both DDR byte lanes.

#### Outcome:

Revision-C artifact `5cbc9bdf0b23d4f2c8e4b1712b33e639792ad21d1b6618e0b2053042a413f047` used diagnostic ABI 1.6 and wrote complementary phase-invariant patterns `0xa55aa55a` and `0x5aa55aa5`; repeating each 16-bit half on all four DFI phases holds every physical DQ pin constant throughout the BL8 burst, making the result independent of temporal serializer ordering. The image size was 4,524,028 bytes and the upload/readback CRC32 was `0x352e295c`. Gowin placement option 3 met every named clock with zero setup and hold TNS, with reported Fmax of 78.523 MHz for the 75.002 MHz diagnostic clock, 169.249 MHz for the 75 MHz system clock, and 680.851 MHz for the 300 MHz fast DDR clock, while parallel placement options 1, 2, and 4 failed timing and were excluded from hardware testing. Both complementary patterns matched exactly on both byte lanes, each producing exact-match masks `0x0000ffff`, minimum error counts of zero, and a first exact coordinate at bitslip 0/read delay 0 for both lanes; Pattern A produced 257 exact samples on lane 0 and 267 on lane 1, and Pattern B produced 258 and 277. The best captured Pattern-A phase words were exactly `0xa55aa55a`, the complementary comparison toggled all 64 burst bits in each lane, and the diagnostic reached terminal stage `0x800000d3` with failure 0 and feature bitmap `0x1f`. Clock/reset status remained `0x00000011`, and Tang-Control matched all 35 FPGA requests with responses and reported zero drops, timeouts, CRC errors, malformed packets, unexpected responses, or receive-FIFO overflows. DDR command/write strobe timing, DQS activity, per-pin DQ drive, and the read path are therefore viable at 75 MHz/X4, isolating the failure to temporal BL8 data ordering or cycle-boundary handling; the device-side diagnostic passed, and no explicit user acceptance was recorded.

#### Next Steps:

Map each of the eight write serializer slots to its observed read slot with a walking temporal beat.

#### Files Modified:

- gateware/ae350_gate1.py
- software/gate1/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: NOT RUN

---

## 8 COMMIT Unreleased 2026-09-28T11:18:43-07:00

#### Coming From:

None.

#### Purpose:

Map each of the eight BL8 write serializer slots to its observed read slot using a walking temporal beat.

#### Outcome:

Revision-C artifact `f49b7179be2a9a133eec48a6cb3f1d1a1233ae856316ed6712521bd127ad4b4f` used diagnostic ABI 1.7 and independently wrote `0xffff` into each of the eight BL8 temporal slots while holding every other slot at zero; its size was 4,492,284 bytes and Tang-Control verified upload/readback CRC32 `0x726550b9`. Gowin placement option 3 met every named clock with zero setup and hold TNS, with reported Fmax of 84.939 MHz for the 75.002 MHz diagnostic clock, 163.223 MHz for the 75 MHz system clock, and 715.563 MHz for the 300 MHz fast DDR clock, while placement options 1, 2, and 4 failed setup timing and were excluded from hardware testing. Every one of the eight writes produced a detected read burst on both byte lanes (`0x0000ffff`), but no write returned as an exact one-slot pulse (`0x00000000`); total error counts were 100 bits on lane 0 and 112 bits on lane 1, terminal stage was `0x800000d4`, failure was `0x00060003`, and the feature bitmap was `0x7` (JEDEC initialization plus all-slot burst detection on both lanes). The result is not a simple permutation: write slots 0 and 1 read back as all zero, later slots produced partial byte values repeated across multiple read positions, and only write slot 4 produced a full `0xff` byte, duplicated at lane-0 read slots 4 and 6. Clock/reset status remained `0x00000011`, with zero reported transport CRC errors or malformed requests. Together with ABI 1.6, this excludes dead pins, missing strobes, and a fixed BL8 slot permutation. Gowin's `OSER8_MEM` documentation identifies its default `HWL="false"` behavior as making internal `d_up1` one cycle ahead of `d_up0`, with `HWL="true"` aligning them, and the GW5 X4 PHY currently leaves this parameter at its default.

#### Next Steps:

Set `HWL="true"` on the X4 memory serializers and rerun the temporal map before changing delay training or controller logic.

#### Files Modified:

- gateware/ae350_gate1.py
- software/gate1/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 9 COMMIT Unreleased 2026-09-28T11:26:19-07:00

#### Coming From:

None.

#### Purpose:

Test whether setting `HWL="true"` on all quarter-rate `OSER8_MEM` serializers corrects the X4 temporal cycle-boundary corruption.

#### Outcome:

Revision-C artifact `8c428be184c75ef6cfec890aa784d6321edb6b318222ef57df7713335715d7c6` reran ABI 1.7 with `HWL="true"` on all quarter-rate `OSER8_MEM` instances; its size was 4,492,284 bytes and Tang-Control verified upload/readback CRC32 `0x0c4c2300`. Placement option 3 met every named clock with zero setup and hold TNS, with reported Fmax of 84.939 MHz for the 75.002 MHz diagnostic clock, 163.223 MHz for the 75 MHz system clock, and 715.563 MHz for the 300 MHz fast DDR clock, while placement options 1, 2, and 4 failed setup timing and were excluded from hardware testing. All eight writes again produced read bursts on both lanes, but the response changed from irregular corruption to a structured cycle-boundary result: lane 0 returned write slot 5 exactly (exact mask `0x20`), write slots 0 through 5 all placed a full byte at read slot 5, and slots 6 and 7 spilled into repeating even/odd read positions, while lane 1 had no exact write and only write slot 7 produced full bytes, in the odd read positions. Packed total errors were 152 bits on lane 0 and 146 bits on lane 1. Terminal stage was `0x800000d4`, failure was `0x00060003`, feature bitmap was `0x7`, clock/reset status was `0x00000011`, and transport error counters remained zero. The strong, repeatable change confirms that `HWL` controls the failing X4 boundary, but aligning only the serializers leaves the associated `DQS` primitive at its default `HWL="false"`, although that primitive exposes the same HWL mode and its simulation model changes the X4 update phase with it.

#### Next Steps:

Match the `DQS` primitive to `HWL="true"` as a one-variable change and repeat the temporal map.

#### Files Modified:

- third_party/litedram/litedram/phy/gw5ddrphy.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 10 COMMIT Unreleased 2026-09-28T11:30:52-07:00

#### Coming From:

None.

#### Purpose:

Test whether matching the quarter-rate `DQS` primitive to `HWL="true"` alongside the aligned `OSER8_MEM` serializers produces a valid X4 temporal map.

#### Outcome:

Revision-C artifact `d67f35d179f882312ceeb942ce694055596a89713a5ae0bd759e44518fc00baa` reran ABI 1.7 with `HWL="true"` on the quarter-rate `DQS` primitives as well as all `OSER8_MEM` instances; its size was 4,492,284 bytes and Tang-Control verified upload/readback CRC32 `0x7175654e`. Placement option 3 met every named clock with zero setup and hold TNS, with reported Fmax remaining 84.939 MHz for the diagnostic clock, 163.223 MHz for the system clock, and 715.563 MHz for the 300 MHz fast DDR clock, while placement options 1, 2, and 4 failed timing and were not deployed. Matching the DQS primitive to HWL mode removed read-burst detections on both lanes (burst mask `0x0000`); the first six walking writes read as all zero, write slot 6 returned exactly on lane 0/read slot 6, and write slot 7 produced full lane-1 bytes in the odd positions. The exact-mask result was lane 0 `0x40` and lane 1 `0x00`, total errors were 56 and 96 bits, terminal stage was `0x800000d4`, failure was `0x00060003`, feature bitmap was `0x1`, clock/reset status was `0x00000011`, and transport errors remained zero. `DQS.HWL="true"` is therefore excluded for the present X4 read-gate design, and the working DQS primitive mode is its default `false`.

#### Next Steps:

Restore the `DQS` primitive to `HWL="false"` and isolate `HWL="true"` to the DQ serializers only, keeping the DQS and constant-zero DM serializers at their original mode so that data half-word alignment is tested without also moving strobe or mask timing.

#### Files Modified:

- third_party/litedram/litedram/phy/gw5ddrphy.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 11 COMMIT Unreleased 2026-09-28T11:36:01-07:00

#### Coming From:

None.

#### Purpose:

Test whether applying `HWL="true"` only to the sixteen quarter-rate DQ serializers produces a valid X4 temporal map.

#### Outcome:

Revision-C artifact `959d27986aa89974dda4996858b2fb0f1cc3428f60a9ae58bb8e5113ddedea61` reran ABI 1.7 with `HWL="true"` only on the sixteen quarter-rate DQ serializers, while both DQS primitives, both DQS serializers, and the constant-zero DM serializers retained `HWL="false"`; Tang-Control verified upload/readback CRC32 `0x2b4c6443` for the 4,492,284-byte image. Placement option 3 met every named clock with zero setup and hold TNS, while options 1, 2, and 4 failed setup timing and were excluded. Read-burst detection remained perfect for all eight writes on both lanes (`0x0000ffff`), but every captured DFI word was `0xffffffff`: exact masks were zero, both mapping tables were `0xffffffff`, and each lane accumulated 448 bit errors. Terminal stage was `0x800000d4`, failure was `0x00060003`, feature bitmap was `0x7`, and clock/reset status was `0x00000011`. Aligning DQ while leaving the DQS serializer at its default therefore breaks the write drive/strobe relationship completely. With the DQS primitive fixed at its proven `HWL="false"`, three of the four DQS-serializer/DQ-serializer combinations have now been measured: `false/false` is irregular, `true/true` is structured but incomplete, and `false/true` reads all ones.

#### Next Steps:

Measure the remaining complementary `true/false` combination by aligning only the DQS output serializer and restoring the DQ and DM serializers to their default mode.

#### Files Modified:

- third_party/litedram/litedram/phy/gw5ddrphy.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 12 COMMIT Unreleased 2026-09-28T11:40:29-07:00

#### Coming From:

None.

#### Purpose:

Complete the X4 output-serializer HWL matrix by applying `HWL="true"` only to the DQS output serializer.

#### Outcome:

Revision-C artifact `6335c375cd5ec99f587aec3c9bf2d35e226c24f9f8c33d8bd1a8aa19795f9b22` completed the fourth DQS-serializer/DQ-serializer HWL pairing with the DQS primitive fixed at its proven `HWL="false"`: DQS output serialization used `HWL="true"`, while DM and DQ serialization used `HWL="false"`. The image was 4,492,284 bytes and Tang-Control verified upload/readback CRC32 `0x2a7131fb`. Placement option 3 met every named clock with zero setup and hold TNS, with reported Fmax remaining 84.939 MHz for the diagnostic clock, 163.223 MHz for the system clock, and 715.563 MHz for the 300 MHz fast DDR clock, while placement options 1, 2, and 4 failed setup timing and were excluded from deployment. All eight writes produced read bursts on both lanes (`0x0000ffff`), but none returned exactly (`0x00000000`); lane 0 accumulated 121 bit errors and lane 1 accumulated 132, the packed temporal maps were lane 0 `0x00000055` and `0x00800000` with lane 1 remaining zero, and the raw captures showed repeated even-position data and later spill rather than a valid BL8 sequence. Terminal stage was `0x800000d4`, failure was `0x00060003`, feature bitmap was `0x7`, clock/reset status was `0x00000011`, and transport errors remained zero. The complete output-serializer HWL matrix is therefore hardware measured: default/default is irregular, aligned/aligned is structured but incomplete, default/aligned reads all ones, and aligned/default duplicates even positions without an exact result, and the DQS primitive aligned mode is also excluded because it suppresses burst detection.

#### Next Steps:

Restore the most promising aligned/aligned output mode with the DQS primitive at its default, then scan all 256 dynamic write-delay taps using two complementary changing patterns at each tap to prevent stale-data matches.

#### Files Modified:

- third_party/litedram/litedram/phy/gw5ddrphy.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 13 COMMIT Unreleased 2026-09-28T11:49:54-07:00

#### Coming From:

None.

#### Purpose:

Determine whether a narrow write-delay window exists in the aligned/aligned X4 output mode by scanning every dynamic write-delay tap with complementary changing patterns.

#### Outcome:

Revision-C ABI 1.8 artifact `7c2467849a0e9c4fcbf0b7d929bc5ca99cf67235676830eee44e706278b06a4f` restored the most promising output mode, in which the DQS primitive retained `HWL="false"` while the DQS, DM, and DQ `OSER8_MEM` instances used `HWL="true"`; its size was 4,539,900 bytes and Tang-Control verified upload/readback CRC32 `0xbc311ca5`. Placement option 3 was again the only eligible build, with zero setup and hold TNS on every named clock and reported Fmax of 76.641 MHz for the 75.002 MHz diagnostic clock, 152.861 MHz for the 50 MHz input clock, 686.989 MHz for the 300 MHz fast DDR clock, and 179.866 MHz for the 75 MHz system clock; options 1 and 2 failed `sys_clk` setup, and option 4 failed both `sys_clk` and `main_clkout` setup. The diagnostic tested both complementary, phase-changing BL8 patterns at every one of the 256 dynamic write-delay taps on each byte lane, requiring both consecutive writes to match so that stale DDR contents could not produce a false pass. Both lanes detected both read bursts at all 256 taps (`0x01000100`), but neither found an exact tap (`0x00000000`), and both first-exact values were `0xffffffff`. The best result remained tap 0 on both lanes, with 56 combined bit errors on lane 0 and 64 on lane 1 (best errors `0x00400038`, best taps `0x00000000`), and the best raw captures were still strongly repeated across phases rather than becoming more or less correct as delay moved. The test executed all 2,055 expected DFI commands and 1,022 write-delay actions, reaching terminal stage `0x800000d5` with failure `0x00070003`, feature bitmap `0x7`, clock/reset status `0x00000011`, and all Tang-Control transport error counters at zero. This excludes a narrow write-delay window as the cause and leaves the temporal generation and ordering of the X4 serialized write burst, rather than basic DDR pin activity or read capture, as the remaining defect.

#### Next Steps:

Investigate the temporal generation and ordering of the X4 serialized write burst, keeping the DQS primitive at `HWL="false"` and the write-delay scan instrumentation available for validation.

#### Files Modified:

- gateware/ae350_gate1.py
- software/gate1/main.c
- third_party/litedram/litedram/phy/gw5ddrphy.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 14 COMMIT Unreleased 2026-09-28T12:14:20-07:00

#### Coming From:

None.

#### Purpose:

Establish from exact-board vendor sources the physical DDR3 interface width and controller configuration of the Tang Console 138K.

#### Outcome:

Sipeed's Tang Mega 138K example repository at commit `06e7d8b118d345915ab6f257b7c22226f81575cd` contains an official DDR memory test applicable to the Tang Console 138K; its project targets `GW5AST-LV138PG484AC1/I0`, its checked-in generated IP targets device revision C, and its README directs revision-B users to change the device model and regenerate all Gowin-specific IP. The exact PG484 SOM schematic `tang_mega_138k_30354_Schematics..pdf`, SHA-256 `326c45a7ae05e990d5f885fba6ec9b589d966941aa3c00f7d43122d99c068af7`, shows two Hynix `H5TQ4G63EFR-RDC` x16 DDR3 devices and distinct data nets for all 32 bits, and Sipeed's exact-board constraints independently bind `ddr_dq[31:0]`, `ddr_dqs[3:0]`, and `ddr_dm[3:0]` to the PG484 package. Sipeed's controller configuration uses `DQ_WIDTH=3`, which its generated parameters resolve to a 32-bit physical data bus, with `MEMORY_CLK=400` and a `1:4` clock ratio, and the reference top sets `DRAM_NUM=2` and exposes a 256-bit native application read/write datapath, corresponding to four physical byte lanes and a 3.2 GB/s theoretical peak transfer rate at 800 MT/s. The prior x16 assumption came from the LiteX Tang Console platform and nand2mario's DDR framebuffer reference, both of which expose only the first x16 DDR3 device with two DQS/DM lanes; that is a deliberately reduced, proven configuration rather than a PG484 routing limitation. No vendor-controller image was built or deployed, and no new hardware result is claimed by this documentary investigation.

#### Next Steps:

Target Sipeed's full x32/four-lane Gowin DDR3 controller for Tang-PSX, regenerate the IP for the project's revision-B device and Gowin 1.9.11.03 environment, and bridge its native application port into the AE350 system.

#### Files Modified:

None.

#### Status:

- Build: N/A
- Deployment: N/A
- User Test: N/A

---

## 15 COMMIT Unreleased 2026-09-28T12:43:27-07:00

#### Coming From:

Unreleased 04da885

#### Purpose:

Record the unlogged diagnostic ABI 1.9 build, which tested Gowin-generated-interface-style X4 DDR primitive settings, and the resulting project direction.

#### Outcome:

The ABI 1.9 experiment set `HWL="true"` on the two quarter-rate `DQS` primitives and `HWL="false"` on all twenty `OSER8_MEM` instances. It also clocked the DQS output serializers from `DQSW270` and the DQ/DM serializers from `DQSW`, which the source comments describe as matching Gowin's generated GW5AST X4 DDR3 interface. The gateware and firmware sources, including debug identifier `0x00010009` in `gateware/ae350_gate1.py`, were committed without a log entry in `04da885`. The PHY changes exist only as uncommitted edits to `litedram/phy/gw5ddrphy.py` and `test/test_gw5ddrphy.py` in the `third_party/litedram` submodule working tree, which is pinned at upstream `c454a44`. The build targeted `GW5AST-138C` for `GW5AST-LV138PG484AC1/I0`, and every placement variant failed setup timing. Option 1 had `sys_clk` TNS -8.830 ns over 28 endpoints, option 2 had -15.123 ns over 34, option 3 had -0.230 ns over 2, and option 4 had `sys_clk` TNS -355.620 ns over 303 endpoints plus `main_clkout` TNS -7.425 ns over 3. All other named clocks and all hold checks were clean, and the option-3 image SHA-256 was `b6ce79b30857bf691e61c78b65ade74c1fc24b86d7c7f385f0891ed94ed19453`. No variant was deployed, so the last hardware-proven state remains ABI 1.8 artifact `7c2467849a0e9c4fcbf0b7d929bc5ca99cf67235676830eee44e706278b06a4f` from entry 13, and the ABI 1.9 primitive settings have no hardware result. A user-supplied photograph of the installed FPGA shows the markings `GW5AST-LV138PG484AC1/I0`, `2518CA0N`, and `TS0E44.00`, whose fifth lot/date character identifies device revision C and corrects the `2518CAON` transcription in entry 1. The user therefore directed that Tang-PSX target revision C, the revision of the physical board, which matches every artifact deployed in entries 1 through 13 and Sipeed's generated DDR3 IP. This supersedes the entry 14 plan to regenerate that IP for revision B, and the stale revision-B line in `.ai/core.md` is left for the user to correct separately. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, and validated this entry against the template. The audit found that settled entries 1 through 14 use `None.` rather than a base reference under Coming From and that the Current Log Conformance section of `.ai/core-syntax.md` still names the Tang-Phosphor log; neither was edited.

#### Next Steps:

Import Sipeed's revision-C x32 Gowin DDR3 controller IP and exact PG484 32-DQ, four-DQS, and four-DM pin mapping, and verify or regenerate the IP with Gowin EDA 1.9.11.03. Build a standalone image that contains only the controller, a four-lane fabric memory test, and Tang-Control-readable status, and prove it on hardware before bridging the controller's 256-bit native port into the AE350 system.

#### Files Modified:

None.

#### Status:

- Build: FAIL
- Deployment: NOT RUN
- User Test: NOT RUN

---

## 16 COMMIT Unreleased 2026-09-28T13:17:15-07:00

#### Coming From:

Unreleased 69e9aa2

#### Purpose:

Prove Sipeed's full x32 Gowin DDR3 controller configuration on the Tang Console 138K with a standalone Tang-Control-observable memory test before bridging it to the AE350.

#### Outcome:

The Gowin DDR3 controller now works on hardware at full speed and full width, completing 232 error-free passes over the full 1 GiB x32 array. The controller configuration from Sipeed's example at `06e7d8b` was regenerated for revision C with Gowin EDA 1.9.11.03, which ships DDR3 IP version 5.9 where Sipeed used version 6.0 from 1.9.12.02_SP1. Headless `gw_sh` `read_ipc` segfaults, so `scripts/gen-ddr3-ip.sh` configures the IP through `create_ipc`, the `set_property` values in `gateware/ddr3_vendor/ddr3_ip.tcl`, and `generate_target`, and it fails if the emitted `.ipc` differs from the committed `gateware/ddr3_vendor/ddr3_memory_interface.ipc`. That `.ipc` matches Sipeed's for every option the older generator supports, and the generated port list is identical. Sipeed's version-6.0-only arbitration, AXI, and memory-controller BSRAM options are unavailable, and the generator ignores the write-recovery setting at this configuration and keeps its default. `GowinModGen` regenerates Sipeed's 400 MHz PLL from the committed `gowin_pll.mod` with an identical body, and `PLL_INIT` is taken from the Gowin installation. Following the earlier licensing decision, the generated encrypted Gowin RTL is kept out of Git and only configuration, constraints, project RTL, and scripts are committed. The DDR3 pin map and placement constraints are Sipeed's (Apache-2.0) with the dock LED and key constraints removed. `gateware/ddr3_vendor/ddr3_tester.sv` writes and then verifies every BL8 burst of the array in order and complements the pattern on alternate passes, so every cell toggles; it issues commands only when the controller is ready and records per-byte error masks plus the expected and observed data at the first failure. A handshake snapshot carries that status to a register file served by `iosys_bl616` as core `0x51`, magic `0x54504433` (`TPD3`), ABI 1.0, clocked from the 50 MHz board clock. The Verilator testbench `gateware/sim/ddr3_tester_tb.sv` passed against a random-ready, random-latency controller model, detecting an injected single-byte fault with the correct burst, pass, and mask. The first four-variant build failed setup timing on every placement option because the tester's pass-seed arithmetic fed its 256-bit generator in one cycle; after that was pipelined, options 2 and 3 met all setup and hold timing, while options 1 and 4 still failed `sysclk` setup on tester generator-enable paths (TNS -120.475 ns over 108 endpoints and -344.459 ns over 281). Option 2, which had the best timing (Fmax 105.149 MHz on the 100 MHz controller clock and 79.748 MHz on the 50 MHz board clock), was deployed as `cores/console138k/tang-psx-ddr3.bin`. That image was 4,651,004 bytes with SHA-256 `a97e9a332807a9874df7256c865336f6b64f241d50204231d829e61a12076f0c`, and Tang-Control verified its SD readback CRC32 `0x0bc2521f`. On hardware the PLL locked and calibration completed 27.306 ms after reset. By the final reading, about 2.9 minutes after load, the test had completed 232 full-array passes with zero error bursts, a zero byte-error mask, and no read stalls. The last sweeps measured 2,853 MB/s write and 2,928 MB/s read, about 89 to 92 percent of the 3.2 GB/s theoretical peak. Tang-Control matched all 69 FPGA requests with responses and reported zero timeouts, CRC errors, malformed packets, unexpected responses, or receive-FIFO overflows. The user accepted the result. The test does not exercise masked writes, interleaved read and write traffic, or scattered addressing, which AE350 traffic will need. Its 32-bit uptime, heartbeat, and verified-burst counters wrap, so `tools/ddr3_vendor_status.py` reports coverage from the pass counter. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, and validated this entry against the template.

#### Next Steps:

Reevaluate the plan with the user before continuing. The standing direction is to bridge the controller's 256-bit native port, clocked at 100 MHz, into the AE350 system and extend DDR testing to masked writes, interleaved traffic, and executable-code coherency.

#### Files Modified:

- README.md
- gateware/ddr3_vendor/ddr3_ip.tcl
- gateware/ddr3_vendor/ddr3_memory_interface.ipc
- gateware/ddr3_vendor/ddr3_tester.sv
- gateware/ddr3_vendor/gowin_pll.mod
- gateware/ddr3_vendor/status_snapshot.sv
- gateware/ddr3_vendor/tang_psx_ddr3.cst
- gateware/ddr3_vendor/tang_psx_ddr3.sdc
- gateware/ddr3_vendor/tang_psx_ddr3_top.sv
- gateware/sim/ddr3_tester_tb.sv
- scripts/build-ddr3-vendor.sh
- scripts/gen-ddr3-ip.sh
- scripts/gowin-timing-summary.py
- tools/ddr3_vendor_status.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 17 COMMIT Unreleased 2026-09-28T14:04:13-07:00

#### Coming From:

Unreleased 2402721

#### Purpose:

Connect the AE350 to the full x32 DDR3 array through the proven Gowin controller and rerun the Gate 1 checks from the AE350.

#### Outcome:

The AE350 now uses the full 1 GiB DDR3 through the proven Gowin controller, and every Gate 1 check passed on hardware with the 75 MHz system clock. The Gowin controller from entry 16 replaced LiteDRAM's controller and PHY in the Gate 1 SoC, and the user-selected direct path was used. The AE350's 64-bit RAM AHB port reaches memory through LiteX's `AE350RAMBridge`, then a LiteDRAM 75-to-100 MHz native-port crossing with a registered read-data stage, then the new `gateware/ddr3_vendor/gowin_ddr3_native.sv` adapter. That adapter holds each command until the Gowin controller can take it, pairing write data with its command in one cycle, and issues reads only against guaranteed return-FIFO credit. `gateware/gowin_ddr3.py` wraps the regenerated controller, its 400 MHz PLL, and the x32 PG484 pin and placement constraints for LiteX, and main RAM is mapped at `0x40000000`. The old experiment was removed: the LiteDRAM DFI command injector, the calibration counters, the PHY-scan CSRs and firmware, and the `liblitedram` link. The `third_party/litedram` working-tree edits were left untouched. The Gate 1 diagnostic ABI is now 2.0 and is documented in `README.md`. The Verilator testbench `gateware/sim/gowin_ddr3_native_tb.sv` passed 20,000 randomized commands, including byte-masked writes and write data that trails its command, and it failed as intended when mask polarity or read credit was deliberately broken. Removing LiteDRAM also silently disabled LiteX's registered CSR bridge, which LiteX enables only when a LiteDRAM core exists. That left every placement variant failing `sys_clk` setup on CSR and DDR read-return paths. A crossbar interconnect made timing worse and was reverted; forcing the registered bridge and adding the read-data register stage fixed it. The first deployed image met timing only with option 4, but it stopped at stage `0x80010001` because the CPU-visible DDR3 status CSR read 0 while the diagnostic copy showed calibration complete. The cause was that `CSRStatus` fields were left undriven after the whole status word was assigned; the fields are now driven directly. In the rebuild only placement option 2 met all setup and hold timing, with `sys_clk` Fmax 76.180 MHz and `ddr_clk` Fmax 131.217 MHz. Options 1, 3, and 4 failed `sys_clk` setup with TNS -26.232 ns over 41 endpoints, -0.742 ns over 1, and -9.758 ns over 27, with the remaining paths in LiteX's AE350 RAM-bridge logic. The option-2 image was 4,952,572 bytes with SHA-256 `bb86dfabab492a7a4aafc2389037e72b753e1c2102c641d7750bbe649b9b110f`, and Tang-Control verified its SD readback CRC32 `0x3dc91a28`. On hardware, calibration completed in 28.6 ms and firmware reached final stage `0x80000001` with failure 0 and feature bitmap `0x3f`. It verified 262,144 fixed-pattern words with checksum `0xd42cc044`, walked every address bit across 1 GiB, verified byte and halfword stores into all 32 lanes of the controller word, and verified interleaved read-after-write. Runtime-written code in DDR returned 42 and then 99 after `fence rw,rw` plus `fence.i` (`0x002a0063`), and the read-return overflow flag stayed clear. Clock status was `0x0000001d`, the run took `0x1cbd0234` AE350 cycles, and Tang-Control matched all 25 requests with zero transport errors. The user accepted the result. The path issues one 32-bit access at a time, about 1 µs per access, so performance is unproven. The AE350 caches are neither enabled nor checked, so the `fence.i` result does not prove cache coherency. Only one of four placement variants meets 75 MHz. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, and validated this entry against the template.

#### Next Steps:

Reevaluate with the user before continuing. Open work includes enabling the AE350 caches and proving code coherency with them on, improving DDR access throughput and the thin 75 MHz `sys_clk` margin, and the R3000A and GTE tests that complete Gate 1.

#### Files Modified:

- README.md
- gateware/ae350_gate1.py
- gateware/ddr3_vendor/gowin_ddr3_native.sv
- gateware/gowin_ddr3.py
- gateware/sim/gowin_ddr3_native_tb.sv
- scripts/build-gate1.sh
- scripts/gen-ddr3-ip.sh
- scripts/gowin-timing-summary.py
- software/gate1/Makefile
- software/gate1/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 18 COMMIT Unreleased 2026-09-28T14:20:52-07:00

#### Coming From:

Unreleased 6e49e01

#### Purpose:

Enable the AE350 instruction and data caches, prove DDR3 access and runtime-code coherency with them active, and measure the effect on DDR3 throughput.

#### Outcome:

With the AE350 instruction and data caches enabled, every Gate 1 check still passed on hardware, and DDR3 throughput rose about 27 to 28 times. At the user's request, `.ai/core-reference.md` was created as the project's structured technical reference, holding records for the device, board, DDR3 controller, AE350, toolchain, and Tang-Control transport facts established so far. The cache control CSRs were not in the reference, so they were looked up online. The sources were Andes Technology's code in upstream U-Boot (commit `1d29c718`), OpenSBI's Andes AE350 platform (commit `06af8bd6`), and enjoy-digital's hardware-tested AE350 caches in litex_wr_nic PR #103 (merge `05df6f4e`), and they were recorded as AE350-002 and AE350-003. Only the firmware changed, and no gateware changed. It times an uncached 1 MiB write and read, enables both caches with `mcache_ctl` (CSR `0x7ca`), and fails if the enable bits do not read back or the CCTL CSRs are absent. It then reruns the fixed-pattern, walking-address, byte-lane, interleave, and code-execution checks, writing back and invalidating the D-cache (`mcctlcommand` `0x7cc` command 6) before each verify so checked data is read from DDR3, and it logs the cache configuration, cycle counts, and a stale-fetch probe. All four placement variants were built. Only option 2 met all setup and hold timing (`sys_clk` Fmax 76.180 MHz); options 1, 3, and 4 failed `sys_clk` setup with the same results as entry 17. The option-2 image was 4,952,572 bytes with SHA-256 `533c9cb602045887934710c49a21a8fea0578b571dd7b7e2911b35209f50b696`, and Tang-Control verified its SD readback CRC32 `0x70e53eb6`. On hardware the firmware reached stage `0x80000001` with failure 0 and feature bitmap `0x7f`, and the fixed-pattern checksum stayed `0xd42cc044`. `mcache_ctl` read back `0x3`, `micm_cfg` and `mdcm_cfg` both read `0x00439ada` (both caches present, inferred to be 32 KiB each), and `mmsc_cfg` read `0x2007f039` (CCTL CSRs present, no programmable PMA). A 1 MiB write took 205,521,436 cycles uncached and 7,601,916 cycles cached including the write-back. A 1 MiB read took 264,241,456 cycles uncached and 9,338,778 cycles cached. At the 750 MHz core clock that is about 3.8 MB/s and 3.0 MB/s uncached against about 103 MB/s and 84 MB/s cached. Code rewritten without `fence.i` still returned the stale value 42, which shows the I-cache holding the old instructions. After `fence rw,rw` plus `fence.i` it returned 99 (`0x002a0063`), which proves instruction-fetch coherency with both caches active. The CSR window at `0xE8000000` remained coherent with the D-cache on, and Tang-Control matched all 49 requests with zero transport errors. The user accepted the result. AE350-002 was promoted to VERIFIED, AE350-003 was corrected to INFERRED because its source is secondary, and AE350-004 records this board's cache configuration and `fence.i` behavior. The only placement variant meeting 75 MHz remains the thin timing margin recorded in entry 17. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, validated this entry against the template, and reviewed the new `.ai/core-reference.md` for consistency with `.ai/core.md`.

#### Next Steps:

Reevaluate with the user before continuing. The remaining Gate 1 work is the R3000A and GTE tests, and the open engineering items are the thin 75 MHz `sys_clk` margin in LiteX's AE350 RAM bridge and further DDR3 throughput for the JIT.

#### Files Modified:

- README.md
- software/gate1/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---
