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

## 19 COMMIT Unreleased 2026-09-28T15:21:09-07:00

#### Coming From:

Unreleased d1f14fb

#### Purpose:

Close 75 MHz system-clock timing across the Gowin placement variants by removing the combinational DDR3 path into the AE350 macro, and re-verify Gate 1 on hardware.

#### Outcome:

Three of four placement variants now meet timing with 8 to 16 percent system-clock margin, and Gate 1 re-passed on hardware with no measurable cost to cached DDR3 read throughput. Before starting, the user set the direction for the rest of Gate 1: the PSX GTE will run in software on the AE350, R3000A emulation starts as an interpreter, and Gate 1 closes with interpreter and GTE checks against PC-generated reference vectors, loaded through a Tang-Control stream loader. The user made timing closure the first priority. In entry 18's build, every failing `sys_clk` path ran from the DDR3 Wishbone address through LiteDRAM's burst frontend and the bursting AHB bridge into the AE350 macro's RAM-port inputs, which alone take about 5 ns. The new `gateware/ae350_ram_bridge.py` provides `Gate1RAMBridge`, a copy of LiteX's `AE350RAMBridge` with a full `WishboneRegisterSlice` on the DDR3 path. That slice alone removed every path into the macro but left all four variants failing inside LiteDRAM's frontend, whose full-address merge and read-cache compares drive its 256-bit write buffer. `BurstWishbone2Native` replaced that frontend. It merges write beats and serves read beats from the fetched 256-bit word based on burst type and previous lane, with no address compares. `gateware/gowin_ddr3.py` also registers write data in the DDR3 clock domain ahead of the adapter. The Migen simulation `gateware/sim/test_ae350_ram_bridge.py` passed 7,506 beats and 3,750 checked reads covering byte-select single writes, wrapping bursts from any lane, and linear bursts crossing a native word. It failed as intended with a broken lane-boundary rule, missing byte selects, or dropped read data. Its read-invalidate-on-write and lane-replace guards are unreachable with legal bursts and remain as defensive logic. Placement options 1, 2, and 3 met all setup and hold timing with `sys_clk` Fmax 86.789, 82.805, and 85.306 MHz. Option 4 failed one `sys_clk` endpoint by 0.522 ns on a LiteX bus-arbiter path into the fabric SRAM. The tightest remaining paths are in Tang-Phosphor's `iosys_bl616` on the 75 MHz diagnostic clock. Option 3 had the best worst-case margin (+8.3 percent on the diagnostic clock, +13.7 percent `sys_clk`, +12.1 percent DDR3 user clock) and was deployed. That image was 4,809,724 bytes with SHA-256 `0a6fb6d912c7711a197142ddf83ad758a81cb34aa6361fca596fb00b71813ef2`, and Tang-Control verified its SD readback CRC32 `0x2f6a8fc7`. With firmware unchanged from entry 18, the hardware reached stage `0x80000001` with failure 0, feature bitmap `0x7f`, checksum `0xd42cc044`, JIT result `0x002a0063`, and stale-fetch probe 42, and Tang-Control matched all 49 requests with zero errors. Cached 1 MiB writes took 7,722,840 core cycles, 1.6 percent more than entry 18's 7,601,916. Cached reads took 9,338,882 cycles, effectively unchanged. The uncached counts (205,521,436 and 264,241,456) were identical to the cycle to entry 18 despite the different DDR3 path, exactly 784 and 1,008 core cycles per word. That shows uncached ROM-resident loops are instruction-fetch bound. Entry 18's uncached "3.8 MB/s and 3.0 MB/s" figures and its 27 to 28 times DDR3 speedup therefore measured caches-off instruction fetch, not DDR3 throughput; this entry corrects them. The user accepted the result. `.ai/core-reference.md` gained AE350-005 (uncached code is fetch bound), AE350-006 (AE350 RAM-port input timing), and TOOL-006 (LiteDRAM burst-frontend timing), and `README.md` describes the new bridge. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, and validated this entry and the reference file.

#### Next Steps:

Implement the approved loader cycle: a Tang-Control stream receiver feeding a 64 KiB block-RAM buffer, a debug-writable AE350 reset register, and a ROM loader that runs the Gate 1 self-checks and then copies, verifies, and executes CRC-checked images from DDR3, with a host tool and simulation, built across all four placement variants.

#### Files Modified:

- README.md
- gateware/ae350_gate1.py
- gateware/ae350_ram_bridge.py
- gateware/gowin_ddr3.py
- gateware/sim/test_ae350_ram_bridge.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 20 COMMIT Unreleased 2026-09-28T15:50:47-07:00

#### Coming From:

Unreleased b75e209

#### Purpose:

Implement and prove the Tang-Control loader cycle for CRC-checked AE350 programs executed from DDR3 after the Gate 1 self-checks.

#### Outcome:

Gate 1 now receives Tang-Control streams through a 512-word approximately 2 KiB flow-controlled clock-domain-crossing FIFO, which the user explicitly selected in place of the superseded 64 KiB block-RAM-buffer idea after the hardware result; the 50 MHz board clock now drives the independent diagnostic and stream domain because the expanded `iosys_bl616` path did not meet 75 MHz. ROM firmware drains tagged start, data, end, and cancel entries, validates a 32-byte header, DDR3 range, stream length, and IEEE CRC-32, writes back and invalidates the D-cache, executes `fence.i`, calls the loaded RV32 entry through the versioned API in `software/common/tpx_api.h`, publishes its return value, and waits for another image. A debug write at address `0x100` holds the AE350 in reset for 31 system clocks without resetting the stream receiver. `tools/ae350_run.py` packages, uploads, streams, resets, and reports images, while `scripts/build-programs.sh` deterministically builds the small `hello` and 49 KiB `blob` checks. The stream simulation passed 60 randomized sessions and 395 ordered entries with asynchronous clocks, backpressure, partial words, cancellation, and the next-session-start boundary, and both programs built with payload CRC-32 values `0x2fdb7c81` and `0x110e21a3`. Three of four placement variants met all setup and hold timing; option 2 had Fmax values of 85.949 MHz for the 75 MHz system clock, 122.025 MHz for the 100 MHz DDR clock, and 77.955 MHz for the 50 MHz board clock, while option 4 failed system-clock setup. The deployed option-2 image was 4,952,572 bytes with SHA-256 `2f119e02544d4a4fec24eb43ff930d8e694677f31a5567c8334c75cd4fbcb6b2`, and Tang-Control verified SD readback CRC32 `0x5dc72edf`. On hardware the unchanged Gate 1 checks passed before loading; `hello.tpx` logged from DDR3 and returned `0x600d0001`, `blob.tpx` streamed at 301.6 KiB/s and returned its expected embedded-data CRC `0x5bf4bf30`, and the debug-reset path reran all self-checks and then executed `hello.tpx` again. The final transport snapshot matched all 353 FPGA requests with responses and reported zero timeouts, CRC errors, malformed packets, unexpected responses, or receive-FIFO overflows; the loader reported three sessions, 49,746 bytes, three ends, no cancels, and no stream overflow. The user accepted the result. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, validated this entry against the canonical template and 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate the plan with the user before continuing; the remaining Gate 1 work is to implement the approved R3000A interpreter and software GTE checks against PC-generated reference vectors and run them through the proven loader.

#### Files Modified:

- README.md
- gateware/ae350_gate1.py
- gateware/sim/test_stream_loader.py
- gateware/stream_loader.py
- scripts/build-programs.sh
- software/common/tpx_api.h
- software/gate1/Makefile
- software/gate1/main.c
- software/programs/blob/blob.S
- software/programs/blob/main.c
- software/programs/common/crt0.S
- software/programs/common/linker.ld
- software/programs/hello/main.c
- tools/ae350_run.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 21 COMMIT Unreleased 2026-09-28T16:20:59-07:00

#### Coming From:

Unreleased c626b23

#### Purpose:

Add and prove deterministic HDMI color bars as the first visible Tang-PSX milestone while preserving the working Gate 1 DDR3 and program-loader paths.

#### Outcome:

At the user's direction, HDMI output moved ahead of the R3000A interpreter and software GTE work so that subsequent development has immediate visual feedback. `Gate1CRG` now generates a 125 MHz serializer clock from the existing system PLL and divides it by five for a 25 MHz pixel domain; the Gowin HDMI PHY drives standard 640x480 blanking at 59.52 Hz and an eight-bar LiteX diagnostic pattern directly from FPGA logic without consuming DDR3 bandwidth. The six relevant upstream video timing and color-bar tests and the two dedicated color-bar tests passed. All four placement variants met setup and hold timing. Option 4 was selected for its balanced margins, with reported Fmax values of 83.614 MHz for the 75 MHz system clock, 117.211 MHz for the 100 MHz DDR clock, 72.462 MHz for the 50 MHz board/diagnostic clock, and 134.725 MHz for the 25 MHz pixel domain; the dedicated 125 MHz OSER10 serializer path has no fabric Fmax report. The deployed option-4 image was 5,082,310 bytes with SHA-256 `50efcce467a59e434b403155b847990add5c840e460ff72dccc11ae80a60c71a`, and Tang-Control verified its SD readback CRC32 `0x13bd5fc4`. The user saw the expected color bars on the HDMI display. The post-video hardware regression reached Gate 1 stage `0x80000001` with failure 0 and feature bitmap `0x7f`; `hello.tpx` returned `0x600d0001`, `blob.tpx` returned its expected `0x5bf4bf30`, and the loader reported two sessions, 49,577 streamed bytes, two ends, no cancels, and no overflow. Tang-Control matched all 167 FPGA requests with responses and reported zero timeouts, CRC errors, malformed packets, unexpected responses, USB drops, or receive-FIFO overflows. `README.md` now distinguishes this self-contained diagnostic output from the planned DDR-backed PSX display path. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, validated this entry against the canonical template and 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate the plan with the user before continuing; the proposed next visible milestone is DDR-backed, line-buffered scanout from PSX VRAM, after which the R3000A interpreter and software GTE checks can use the proven program loader.

#### Files Modified:

- README.md
- gateware/ae350_gate1.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 22 COMMIT Unreleased 2026-09-28T16:41:50-07:00

#### Coming From:

Unreleased 168d8d5

#### Purpose:

Replace the fixed HDMI color bars with a software-filled, DDR3-backed framebuffer and prove concurrent AE350 and video access on hardware.

#### Outcome:

Gate 1 now scans a contiguous 640x480 RGB565 surface at `0x7ff00000` through a 4 KiB FIFO while the AE350 retains read/write access to the same vendor DDR3 controller. The new fair two-client native-port arbiter alternates CPU and video commands when both are active and uses a 16-entry ownership FIFO to route in-order read returns; its randomized simulation routed 192 interleaved reads with no same-client run longer than two requests and preserved a CPU write. A second simulation delivered 64 ordered RGB565 pixels from four 256-bit DDR words across the system-to-HDMI clock crossing without underflow, while the existing RAM-bridge, stream-loader, 15 relevant LiteX framebuffer/timing tests, program builds, and generation-only build passed. ROM firmware draws a bordered gradient/checker image, writes back the data cache, enables video DMA, and reports framebuffer feature bit 7; the TPX header exposes the address, dimensions, and stride so loaded programs can draw and flush pixels. Debug ABI `0x00020001` adds video status at `0xf4`, and `tools/ae350_run.py` reports DMA enable and sticky underflow. Of four placement variants, only option 3 met all setup and hold constraints, with Fmax values of 87.408 MHz for the 75 MHz system clock, 115.594 MHz for the 100 MHz DDR clock, 63.760 MHz for the 50 MHz board/diagnostic clock, and 138.360 MHz for the 25 MHz pixel domain; options 1, 2, and 4 respectively missed DDR or system setup and were rejected. The deployed option-3 image was 4,894,608 bytes with SHA-256 `f2be953a8a2f0d117a59f1ebb57c7751a889d44ff5d349d5d2c08079b08eeadc`, and Tang-Control verified SD readback CRC32 `0x16fc0c8d`. The user saw the expected gradient/checker image with its white border and black center cross. On hardware Gate 1 reached stage `0x80000001` with failure 0 and feature bitmap `0xff`; video remained enabled with no sticky underflow before and after concurrent loader traffic, `hello.tpx` returned `0x600d0001`, and the 49 KiB `blob.tpx` returned `0x5bf4bf30`. Tang-Control matched all 170 FPGA requests with responses and reported zero timeouts, CRC errors, malformed packets, unexpected responses, USB drops, or receive-FIFO overflows. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, validated this entry against the canonical template and 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate the plan with the user before continuing; the recommended next milestone returns to the R3000A interpreter and software GTE reference-vector checks through the proven loader, followed by PlayStation 1024x512 15-bit VRAM scanout and the GPU/BIOS command path needed for the startup logo.

#### Files Modified:

- README.md
- gateware/ae350_gate1.py
- gateware/ddr3_port_arbiter.py
- gateware/sim/test_ddr3_port_arbiter.py
- gateware/sim/test_video_framebuffer.py
- software/common/tpx_api.h
- software/gate1/main.c
- tools/ae350_run.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 23 COMMIT Unreleased 2026-09-28T17:03:31-07:00

#### Coming From:

Unreleased eebc369

#### Purpose:

Implement the approved R3000A interpreter and software GTE reference-vector diagnostic, run it through the proven loader, and provide immediate HDMI confirmation.

#### Outcome:

Gate 1 now has a reusable portable PlayStation execution foundation in `software/psx`: the MIPS I interpreter covers integer arithmetic, shifts, HI/LO multiply and divide, immediate operations, little-endian byte/halfword/word and unaligned loads and stores, jumps and branches, COP0, COP2 transfers, load and branch delay slots, and exception Cause, EPC, BD, and BadVAddr state. The initial software GTE subset implements data/control transfers, MVMVA, RTPS, RTPT, NCLIP, AVSZ3, and AVSZ4; command timing, lighting/color operations, and untested saturation edges remain outside this milestone and it is not yet a BIOS-capable machine emulator. `tools/gen_psx_vectors.py` deterministically generated nine CPU and six GTE cases, and the native regression passed all 96 selected state checks with vector CRC-32 `0xc2527637` and state checksum `0x9349f3af`; AddressSanitizer and undefined-behavior checks passed, repeat generation was identical, and an altered ADDU was detected by the mutation check. The complete program-build regression passed for `hello`, `blob`, and `psx_diag`. The packaged image contained a 13,652-byte payload with CRC-32 `0x15f3b1a0`; the 13,684-byte TPX had SHA-256 `8a3b82829650951c3979fcb9371630ea8f19cad39630a2518e7da8136019ac44` and SD readback CRC-32 `0x8785f13b`. Streaming took 55 ms through the flow-controlled FIFO. On the AE350 all 96 checks passed in `0x003f7588` core cycles, returning `0x3000a001` with stage `0x80003001`, failure zero, vector counts `0x00090006`, checksum `0x9349f3af`, no failure detail, and no loader overflow, cancellation, or HDMI underflow. Tang-Control matched all 62 requests with responses and reported zero timeouts, CRC errors, malformed packets, unexpected responses, USB drops, or receive-FIFO overflows. The user saw and accepted the green-and-blue `PSX CPU` / `GTE PASS` framebuffer card. `.ai/core-reference.md` gained PSXCPU-001 from the MIPS R3000 manual plus PCSX-Redux and GTE-001 from PSX-SPX plus PCSX-Redux; the GTE record remains INFERRED because no original PlayStation hardware comparison was made. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, validated this entry and the reference additions, confirmed 23 sequential entries and exactly six required sections, inspected the complete `.ai` diff, and confirmed that no settled history was rewritten.

#### Next Steps:

Gate 1 is complete at its approved interpreter-and-GTE diagnostic boundary; reevaluate with the user before starting the next milestone, with the recommended path being PlayStation 1024x512 15-bit VRAM scanout and the GPU/BIOS memory-map and command path needed to begin executing the startup sequence.

#### Files Modified:

- README.md
- scripts/build-programs.sh
- software/programs/psx_diag/diag.c
- software/programs/psx_diag/diag.h
- software/programs/psx_diag/main.c
- software/psx/gte.c
- software/psx/psx.h
- software/psx/r3000.c
- tests/psx_diag_host.c
- tests/test_psx_diag.py
- tools/gen_psx_vectors.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 24 COMMIT Unreleased 2026-09-28T17:51:09-07:00

#### Coming From:

Unreleased 98a0ba9

#### Purpose:

Implement the approved PlayStation BIOS memory map, DMA and software-GPU path, then prove the authentic SCPH-1001 startup logo through the existing Gate 1 loader and HDMI framebuffer.

#### Outcome:

The portable R3000A now supports callback-based buses and hardware interrupt entry while preserving the 96/96 CPU/GTE regression. `software/psx/machine.c` supplies mirrored 2 MiB RAM, scratchpad and BIOS mappings, cache-isolation behavior, IRQ/VBlank state, startup timer/CD-ROM/SPU registers, GPU linked-list/block DMA, and ordering-table DMA; `software/psx/gpu.c` supplies 1024x512 BGR555 VRAM, GP0/GP1 parsing, transfers, drawing state, fills, polygons, lines, rectangles, texture sampling, and HDMI conversion. The external 524,288-byte SCPH-1001 image was accepted only at SHA-256 `71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3` and was not committed. The sanitizer-backed host regression executed 99,999,544 guest instructions with 10,768 GPU words, 414 primitives, 63 uploads, 158,497 DMA words, zero unknown GPU commands or I/O accesses, and deterministic framebuffer SHA-256 `0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7`. The 546,792-byte TPX had SHA-256 `40d293e2fdb0dff9e73118c5f515d39d1e80a5b11d7b8776717bd8dd2dfcb2e1`; Tang-Control uploaded it at 330.2 KiB/s and verified SD CRC-32 `0x30bd0a49`. On the unchanged hardware-proven Gate 1 image, the BIOS progressed through black, brightening gray, the animated orange/red diamond, and the complete blue `SONY` / `TM` / `COMPUTER ENTERTAINMENT` screen, returning `0xb1051001` at stage `0x80011001` with telemetry exactly matching the host run, failure zero, and no stream overflow or HDMI underflow; the user reported that everything booted perfectly. `.ai/core-reference.md` gained PSXGPU-001 and PSXIO-001 from PSX-SPX and PCSX-Redux; both remain INFERRED because timing and behavior beyond this real-BIOS startup path were not compared with original PlayStation hardware. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, validated this entry and the reference additions, confirmed 24 sequential entries and exactly six required sections, inspected the complete `.ai` diff, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate the next milestone with the user; the recommended path is to continue beyond the logo into the BIOS menu by completing CD-ROM, controller, memory-card, audio, GPU-command, and peripheral timing behavior, while keeping the current deterministic logo checkpoint as a regression.

#### Files Modified:

- README.md
- scripts/build-programs.sh
- software/programs/psx_bios/bios.S
- software/programs/psx_bios/main.c
- software/psx/gpu.c
- software/psx/gpu.h
- software/psx/machine.c
- software/psx/machine.h
- software/psx/psx.h
- software/psx/r3000.c
- tests/psx_bios_host.c
- tests/test_psx_bios.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 25 COMMIT Unreleased 2026-09-28T20:12:05-07:00

#### Coming From:

Unreleased f0245ea

#### Purpose:

Measure and improve SCPH-1001 startup performance on the 750 MHz AE350, then begin the user-approved transition from the optimized interpreter to a hybrid dynamic recompiler.

#### Outcome:

The BIOS runner now drives VBlank from detected wait states, stops at the exact established logo checkpoint or a 30-second board-timer cutoff, publishes partial progress, and uses signature-checked accelerators plus a compact interpreter path for the measured BIOS copy, clear, SPU-copy, delay, and bitmap-search loops. A new RV32 dynamic recompiler uses a 512-entry direct-mapped block table and 128 KiB code buffer, hot-block and minimum-length admission, runtime source validation, direct integer ALU, immediate, branch, jump, delay-slot, and guarded mirrored-RAM byte, halfword, and word load/store translation, with interpreter fallback for unsupported, exceptional, BIOS, and MMIO operations. The generated-code differential passed under QEMU for ALU, control flow, load delays, RAM access, self-modifying code, fallback, invalidation, and the first-instruction guard bailout that froze the initial `b9.tpx` deployment; all 96 CPU/GTE checks and mutation detection passed, the complete program build passed, and the sanitizer-backed event-driven host BIOS checkpoint retained framebuffer SHA-256 `0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7`. Hardware `jit4.tpx`, SHA-256 `237fca58c31b2a0411ed1e49bd73045d5733f771521cafd7bbeecf5c3dc36383` and SD CRC-32 `0x2d14c4ab`, measured the compact interpreter and JIT at approximately 6.39 versus 10.90 MIPS on a 16-instruction ALU loop and 5.35 versus 10.01 MIPS on a RAM-heavy loop, or 1.71x and 1.87x speedups, with caches enabled, one compiled block, 62,497 compiled-block executions, and 48 fallbacks. Corrected `b10.tpx`, SHA-256 `5ea6e8b4ffe7099edb7a2f791a0af7abc3cb656ad5194af6a32ff51c69574765` and SD CRC-32 `0x778cf01c`, no longer froze and returned normally at the cutoff with failure 2, 6,776 GPU words, 11 primitives, 12 uploads, 11 VBlanks, 17,913 DMA words, 153 compiled blocks, 14,416 block executions, 149,556 JIT instructions, and 1,177,534 interpreter fallbacks in the final cache generation; this was one VBlank ahead of `b8` but remained behind the optimized-interpreter `b6` result of 13 VBlanks. HDMI DMA reported no underflow, but the user saw only a black screen with corrupted pixels and ended the unsuccessful performance cycle for handoff. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, validated this entry as number 25 with exactly six required sections, inspected the complete `.ai` diff, confirmed the active log remains below 100 entries, and confirmed that no settled history was rewritten.

#### Next Steps:

Continue from the proven `b10` correctness baseline by preventing the small generated-code cache from repeatedly resetting and discarding hot blocks, preserving cumulative JIT statistics across cache generations, and profiling block coverage and bailout causes before adding immutable BIOS loads, indirect jumps, or a less expensive RAM-code invalidation strategy; retain `b6` as the current 30-second performance reference and the exact host framebuffer checkpoint as the correctness oracle, with the eventual target still a roughly 10-second BIOS logo boot.

#### Files Modified:

- scripts/build-programs.sh
- software/programs/psx_bios/main.c
- software/programs/psx_perf/main.c
- software/psx/jit.c
- software/psx/jit.h
- software/psx/machine.c
- software/psx/machine.h
- software/psx/psx.h
- software/psx/r3000.c
- tests/psx_bios_host.c
- tests/psx_jit_rv32.c
- tests/psx_jit_rv32.ld
- tests/psx_jit_rv32_start.S
- tests/test_psx_bios.py
- tests/test_psx_jit.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---
## 26 COMMIT Unreleased 2026-09-28T21:38:24-07:00

#### Coming From:

Unreleased 8ec11cf

#### Purpose:

Measure and remove the bottlenecks that kept the SCPH-1001 logo boot far slower than the roughly 10-second target on the AE350.

#### Outcome:

The SCPH-1001 logo checkpoint now completes on hardware in 5.87 s, after three user-approved plan revisions uncovered first a GPU bottleneck and then a mis-clocked CPU; the user saw the logo come up almost instantly and accepted the result. JIT statistics are now cumulative across cache flushes and count evictions, invalidations, rejected compiles, early exits, and fallback causes, and the interpreter fallback now runs to the end of the current basic block instead of dispatching every instruction. That cut compiles from 2,347 to 832, flushes from 15 to 5, and evictions from 1,953 to 574 to the logo, while JIT coverage fell from 38.6 to 35.9 percent; its separate hardware effect was not measured. `tests/test_psx_bios_jit.py` boots the BIOS to the logo through the real RV32 JIT under `qemu-riscv32` in about 6 s and requires the host telemetry and framebuffer SHA-256 `0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7` exactly; because the service loop is event driven, its JIT counts equal the AE350's. With 64-bit cycle attribution added to the runner, a hardware run of the unchanged dispatcher reached only VBlank 121 of 144 in about 300 s of wall time, 95 percent of it in the software GPU, whose `draw_triangle` evaluated 64-bit edge functions and three to five 64-bit divisions per bounding-box pixel. The rewritten rasterizer steps edge functions incrementally and keeps each attribute as an exact quotient and remainder, so it needs no per-pixel division yet matches the old per-pixel barycentric division bit for bit; `tests/test_psx_gpu.py` compares 40,000 random polygons against the rasterizer pinned at `8ec11cf` after every primitive and detected a planted carry-compare mutation. The same runs showed that the AE350 core had been running at 75 MHz rather than 750 MHz, which also means entry 18's cached and uncached MB/s figures and entry 25's MIPS figures are ten times too high, and that entry 25's `b6`/`b10` comparison used a 30 s cutoff at a 75 MHz core. `software/programs/clock` runs a dependent `addi` chain while publishing its cycle and retired-instruction counts. A clock-probe build that changed only the AE350 PLL dividers moved the core from 75.0 MHz to 49.85 MHz when `CLKOUT1` went from 75 MHz to 50 MHz and `CLKOUT0`, which the netlist connected to `CORE_CLK`, went from 750 MHz to 375 MHz, showing that the AE350 takes its core clock from `PLL_R[0]` `CLKOUT1`. The new local `gateware/ae350_pll.v` therefore generates the 750 MHz CPU clock on `CLKOUT1` and wires `CORE_CLK` to it, and `scripts/build-gate1.sh` gained `TANG_PSX_GATE1_ARGS` and `TANG_PSX_GATE1_NAME` for diagnostic variants. Tang-Phosphor's `src/ae350/ae350_pll.v` at `292ae779da23` has the same bug, since it drives `CORE_CLK` from `CLKOUT0`; at the user's direction it was left unchanged. Of four placements only option 3 met all setup and hold timing, including 750 MHz `cpu_clk`, with the same Fmax figures as entry 22. Its 4,894,608-byte image, SHA-256 `a4b63072169bd62a75b8fe633d66f3d19355ba9c8c3f43b7ef6442f633d0e676` and CRC-32 `7c200621`, replaced `cores/console138k/tang-psx-gate1.bin` on the SD card, and Tang-Control verified the readback. On it the Gate 1 self-checks passed (stage `0x80000001`, features `0xff`), and 2^30 counted cycles finished 1.48 s after stream start, which gives at least 725 MHz at 0.994 instructions per cycle. The BIOS runner now uses `api->cpu_hz` and a 64-bit cycle counter, stops after 30 s, publishes progress once per second, and logs a millisecond timing summary. Hardware image `gpu2.tpx`, 574,048 bytes with CRC-32 `6cdbe204`, returned `0xb1051001` at stage `0x80011001` with no HDMI underflow. Its telemetry matched the host checkpoint exactly (27,870,497 guest instructions, 10,768 GPU words, 414 primitives, 63 uploads, 144 VBlanks, 158,497 DMA words, PC `80047c2c`), and its JIT counts matched QEMU. It spent 5,866 ms in total: 3,753 ms in the GPU, 1,830 ms in other CPU emulation, 127 ms in accelerators, and 125 ms copying the display. VBlank 13 arrived at 1,510 ms. All six host and simulation regressions passed, and the final source rebuilds the tested BIOS image byte for byte. `.ai/core-reference.md` gained AE350-007 for the `CLKOUT1` core clock and TCTL-003, which records that `tangctl status` reports `core_running: no` while a core is loaded, so `active_core` is the indicator. The core-syntax audit re-read `.ai/core.md` (updated by the user to device revision C in `aa6c974`) and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged by this cycle, inspected the complete `.ai` diff, validated this entry as number 26 with exactly six sections, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate the next milestone with the user. The software GPU is now 64 percent of boot time; the untested working hypothesis is that its roughly 47 million VRAM pixel writes are bound by DDR3 access through the 75 MHz AE350 RAM port, so profile that before choosing between faster VRAM access and a fabric rasterizer as the next boot-time lever. Beyond that, the BIOS-menu work from entry 24 (CD-ROM, controllers, memory cards, audio, and peripheral timing) remains.

#### Files Modified:

- README.md
- gateware/ae350_gate1.py
- gateware/ae350_pll.v
- scripts/build-gate1.sh
- software/programs/clock/main.c
- software/programs/psx_bios/main.c
- software/psx/gpu.c
- software/psx/jit.c
- software/psx/jit.h
- software/psx/machine.c
- software/psx/machine.h
- tests/psx_bios_rv32.c
- tests/psx_gpu_diff.c
- tests/test_psx_bios_jit.py
- tests/test_psx_gpu.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---
## 27 COMMIT Unreleased 2026-09-28T22:45:36-07:00

#### Coming From:

Unreleased c3aaf81

#### Purpose:

Boot a commercial disc, Spyro the Dragon (USA), from the Tang's SD card toward its intro, as the user chose over optimizing the GPU further for the BIOS logo.

#### Outcome:

The SCPH-1001 BIOS now boots Spyro the Dragon from the SD card on hardware through the PlayStation logo into the game's own first screen, and the user reported that everything played slowly but properly until the run's 60 s limit ended as the game's "SONY COMPUTER ENTERTAINMENT" text appeared. The work was developed on the host against the user's local copy of the image with the new harness `tests/psx_disc_host.c` and disassembler `tools/mipsdis.py`. The old no-disc CD stub was replaced by `software/psx/cdrom.c`, a controller following PSX-SPX with banked registers, a response queue gated by the interrupt acknowledge, 1x/2x sector timing, 2048/2340-byte delivery, and DMA channel 3. `software/psx/sio.c` adds the controller port with a digital pad, without which the shell hung after its intro. The GTE gained all sixteen lighting and color commands, whose absence made the PlayStation logo's NCDS a reserved-instruction exception. Three emulation faults were also found and fixed. First, DMA interrupts are now raised on the rising edge of the DICR master flag, since level triggering trapped the kernel's CD DMA handler in an interrupt loop. Second, `psx_machine_idle_to` advances the clock to the next frame while the CPU only waits for VBlank, because delivering VBlanks without advancing time let the shell's six-VBlank drive timeout expire before the drive could answer, which sent it to the Main Menu. Third, the display copy precomputes its column map. The no-disc logo regressions kept their exact telemetry and framebuffer SHA-256. The hardware path adds a Gate 1 disc mailbox (request sequence, offset, and length at debug addresses `0x200`-`0x208`, and the disc size written by the BL616 at `0x20c`), widens the debug decode to address bits 9:2, and raises the debug ABI to `0x00020002`. Loader API 2 adds `stream_read`, `disc_request`, and `disc_sectors`. `software/programs/psx_disc` serves the machine from two 32-sector read-ahead windows. In Tang-Control, commit `fbbddc61060a` on its existing `feature/usb-cdc-file-transfer` branch adds `core/tangpsx.cpp`, which publishes the size of the `.bin` named by the first `.cue` in the SD root and streams each requested byte range with range-capable `fpga_file_stream`. That firmware was installed with `tangctl firmware` (app SHA-256 `2efb7242cc83d2d50b7d2401e7458f115541ec3d093741d732157e26483f6b7e`). Of four Gate 1 placements, options 3 and 4 met all setup and hold timing, and options 1 and 2 failed by -0.163 ns (`ddr_clk`) and -0.035 ns (`sys_clk`) on one endpoint each. Option 4 was deployed as `cores/console138k/tang-psx-gate1.bin` (5,066,438 bytes, SHA-256 `d148756ca79b9c7453c8494c27e12b37a3641275d3807894ef6f88e69029b6b8`, CRC-32 `15cd65fc`, verified on SD readback) with Fmax 75.532 MHz on the 75 MHz `sys_clk`, a thinner margin than entry 26's build. Tang-Control published 281,270 sectors for `Spyro the Dragon (USA).bin`. `psx_disc.tpx` (payload 587,580 bytes, CRC-32 `9df87d0d`) returned `0xd15c0001` after 60 s with 685 VBlanks, 496 sectors, 23 requests, no retries, 3,688 sector reads that waited for data, no failed requests, stream cancels, overflow, or HDMI underflow, and 798,541 GPU words, exactly the host run's count at that point. That is about 19 percent of real-time speed. The committed tree rebuilds `psx_disc.tpx` with a behavior-neutral GetlocP cleanup (payload 587,612 bytes, CRC-32 `c81a70f7`). On the host, Spyro later clears low RAM, including the kernel vectors, from a palette-fade routine whose list comes from a raw 2340-byte CD buffer. The data matches the disc and the crash does not move when the drive is made twice as fast or slow, so it is a deterministic emulation difference that is not yet identified. The GPU still lacks semi-transparency, and games using a different display mode appear cropped. `.ai/core-reference.md` gained PSXIO-002 (CD-ROM), PSXIO-003 (DMA interrupt edge), PSXIO-004 (digital pad), GTE-002 (color commands), and TCTL-004 (disc service). The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 27 with exactly six sections, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user. The open items are to find the deterministic divergence that crashes Spyro about three seconds in, and to raise emulation speed from 19 percent of real time by profiling the sector waits and rendering on hardware. Remaining fidelity work is semi-transparency, display-mode handling, root-counter interrupts, and SPU, CD-DA, and XA audio.

#### Files Modified:

- README.md
- gateware/ae350_gate1.py
- scripts/build-programs.sh
- software/common/tpx_api.h
- software/gate1/main.c
- software/programs/psx_disc/bios.S
- software/programs/psx_disc/main.c
- software/psx/cdrom.c
- software/psx/cdrom.h
- software/psx/gte.c
- software/psx/machine.c
- software/psx/machine.h
- software/psx/sio.c
- software/psx/sio.h
- tests/psx_bios_host.c
- tests/psx_disc_host.c
- tests/test_psx_bios.py
- tests/test_psx_bios_jit.py
- tools/mipsdis.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 28 COMMIT Unreleased 2026-09-28T23:16:53-07:00

#### Coming From:

Unreleased d0e895f

#### Purpose:

Identify and correct the deterministic CD-ROM divergence that corrupted Spyro shortly after its first screen, then allow the disc runner to continue until reset.

#### Outcome:

The divergence was the CD-ROM request register's BFRD cursor behavior, not CPU, timing, or disc-stream corruption: Spyro's PsyQ `CdRead` selects 2340-byte sectors, reads the 12-byte Mode 2 header, reasserts BFRD, and then DMA-transfers 2048 data bytes, while `software/psx/cdrom.c` incorrectly reloaded the FIFO on that second enable and delivered the header again. The controller now preserves its cursor while BFRD remains asserted, resets it when BFRD is cleared, selects the next sector when it arrives, and retires a fully consumed buffer. A sanitizer-backed two-sector regression reproduces PsyQ's header/data sequence and passes; the 96 CPU/GTE vectors with mutation detection, 40,000-polygon GPU differential, RV32 JIT checks, exact interpreter and JIT BIOS checkpoints, and a 30-emulated-second Spyro host run all passed. The corrected host run continued through the former approximately 13-second low-RAM corruption point to active rendering and XA-sector handling. The disc program now runs until core reset instead of deliberately returning after 60 wall-clock seconds, and `tools/ae350_run.py run --detach` starts persistent programs without waiting for a terminal result. The rebuilt persistent TPX has a 587,380-byte payload, payload CRC-32 `0x59a5176a`, complete-image SHA-256 `6ed8559e50b848216f7ad1eebac2b412ecda1d13ec82c28fbde4a407287e5c12`, and verified SD readback CRC-32 `0x06a6720e`. On the unchanged Gate 1 core and Tang-Control firmware it passed the former stop and corruption points with zero program failure, stream overflow, or HDMI underflow; telemetry reached 2,435 VBlanks while GPU traffic and disc requests continued, and the user reported unchanged FPS, visible intro polygons, and final arrival at the game's Press Start screen. `.ai/core-reference.md` updates PSXIO-002 with the cursor rule and its PsyQ dependency using PSX-SPX, pinned DuckStation source, and the pinned Spyro decompilation. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 28 with exactly six required sections, confirmed 28 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Continue from the proven Press Start state by testing controller-driven menu and gameplay paths; separately profile the unchanged approximately 19-percent real-time performance and address known rendering fidelity gaps including semi-transparency and display-mode handling, while retaining the new CD-ROM cursor regression and exact BIOS checkpoints.

#### Files Modified:

- README.md
- software/programs/psx_disc/main.c
- software/psx/cdrom.c
- software/psx/cdrom.h
- tests/psx_cdrom_host.c
- tests/test_psx_cdrom.py
- tools/ae350_run.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 29 COMMIT Unreleased 2026-09-29T09:02:06-07:00

#### Coming From:

Unreleased a404030

#### Purpose:

Move PlayStation polygon and rectangle rasterization from AE350 software into an FPGA fabric rasterizer drawing into DDR-resident VRAM, and close Gate 1 timing on the resulting core.

#### Outcome:

The new `gateware/gpu_rasterizer.sv` performs every per-pixel step of the portable renderer: incremental edge functions, exact quotient-and-remainder attribute stepping, texture and CLUT lookups through a two-line cache, shading, and mask handling. It writes the 1024x512 BGR555 VRAM, which now lives in DDR at `0x7fe00000`, through a 256-bit native port. The AE350 still decodes GP0 packets and performs the divide-based triangle setup. `gateware/gpu_accel.py` provides the Wishbone front end at `0xe9000000`: a 128-word descriptor FIFO, status, and completed-primitive and pixel counters. It also contains a batched descriptor DMA, `GPUCommandDMA`. Two `DDR3RWArbiter` instances in `gateware/ddr3_port_arbiter.py` serialize the DMA with the rasterizer, and the accelerator with the CPU, ahead of the existing CPU/video arbiter. `software/psx/gpu.c` streams primitives to the fabric when it is built with `PSX_GPU_ACCEL`, which `scripts/build-programs.sh` now sets for `psx_bios` and `psx_disc`. `psx_gpu_sync` drains the fabric and invalidates cached VRAM before any CPU read. Five cumulative profile registers at `0x110`-`0x120` report CPU, GPU, accelerator, sync, and display milliseconds, and `tools/ae350_run.py status` prints them. Hardware testing in the preceding session found three problems. First, waiting for the DMA-ready bit to fall was edge-sensitive and deadlocked Spyro at VBlank 33, and builds that did not wait for completion showed black and tan corruption. Second, the first DMA-fetched descriptor never completed on hardware, although `gateware/sim/test_gpu_command_dma.py` passes. Third, the pipelined `gate1-gpu-batched-ordering-pipe` place3 core (`bab814d9`) ran even though `sys_clk` failed at 72.62 MHz. Software therefore waits for the completed-primitive counter to reach the number of submitted primitives and writes descriptors directly over MMIO; the DMA remains in the gateware but is unused. This session closed timing from that core's source. A skid buffer now registers the CPU/video arbiter's command in both directions, which broke a valid-to-ready loop running from the scanout reservation FIFO through the DDR command CDC. The Gate 1 CSR read mux is two registered stages, still four cycles per access, and the bridge captures address and data on every idle cycle. In the rasterizer, attribute enables come from registered commit flags, row and column end conditions are registered flags, reset overrides only control registers, and read ready is constant. The accelerator's Wishbone read data is registered, the DDR read-return pipe gained a ready-side stage, and `gateware/ae350_gate1.py` gained `--route-option` with default 1. Gowin's `-replicate_resources` was tried and made timing worse. Of placements 1 to 4 at route option 1, only `build/gate1-gpu-final-place3` met all setup and hold timing: `sys_clk` Fmax 78.144 MHz and `ddr_clk` Fmax 107.693 MHz. Its 5,034,694-byte image, SHA-256 `dba1bf655fcca165d07dd8807f4c15a8186e63eba1f0ac63ad117e1b224e6e06` and CRC-32 `2cf878cc`, rebuilds byte for byte from the final tree. It replaced `cores/console138k/tang-psx-gate1.bin` and was verified on SD readback. The existing `tpx/psx_disc.tpx` (590,516 bytes, CRC-32 `de830b9f`, SHA-256 `8bf6c718340cd8cc70162dee0b6cb10d29dc7a51a646548d2fbb9cec163458db`) also rebuilds identically and was verified on SD readback. The core's self-checks passed with stage `0x80000001` and features `0xff`. Spyro then ran with zero program failure, stream overflow, or HDMI underflow, at 92 VBlanks in 5.0 s from VBlank 220, 203 in 17.0 s from VBlank 312, and 90 in 15.0 s from VBlank 515. In that last window CPU emulation took 13.5 s, the GPU 0.25 s, and the display copy 0.95 s. The user saw the polygon sequence running at about 2 FPS, unchanged from the previous core. All seven gateware simulations passed, including a CSR test extended to read 20 registers across three mux groups. The 40,000-primitive Verilator comparison of the fabric against the portable renderer matched in the same 33,271,453 cycles before and after the timing changes. All seven host regressions passed, including both exact BIOS logo checkpoints. The sanitizer-backed host tests must run without the `LD_PRELOAD` that `scripts/env.sh` exports for Gowin, because AddressSanitizer aborts when its runtime is not first. The `psx_bios` program, now also accelerated with VRAM in DDR, was not run on hardware in this cycle. `.ai/core-reference.md` gained TOOL-007 for the Gowin placement, routing, and replication options. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 29 with exactly six required sections, confirmed that 29 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user. The fabric rasterizer moved rendering off the AE350 but did not change the visible frame rate, because CPU emulation now takes about 90 percent of wall time in Spyro's polygon sequence, so raising JIT coverage and CPU throughput is the next performance lever. Separately, either find why the first DMA-fetched descriptor never completes on hardware although simulation passes, or remove the unused DMA path to recover timing margin, since only one of four placements now meets timing. The accelerated `psx_bios` logo checkpoint still needs a hardware run on this core. Controller-driven gameplay, semi-transparency, and display-mode handling from entry 28 remain open.

#### Files Modified:

- gateware/ae350_gate1.py
- gateware/ddr3_port_arbiter.py
- gateware/gowin_ddr3.py
- gateware/gpu_accel.py
- gateware/gpu_rasterizer.sv
- gateware/sim/test_ddr3_rw_arbiter.py
- gateware/sim/test_gate1_csr.py
- gateware/sim/test_gpu_command_dma.py
- scripts/build-programs.sh
- software/common/tpx_api.h
- software/gate1/main.c
- software/programs/psx_bios/main.c
- software/programs/psx_disc/main.c
- software/psx/gpu.c
- software/psx/gpu.h
- tests/psx_gpu_accel_diff.cpp
- tests/test_psx_gpu_accel.py
- tools/ae350_run.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 30 COMMIT Unreleased 2026-09-29T09:08:26-07:00

#### Coming From:

Unreleased f77f3be

#### Purpose:

Run the fabric-accelerated SCPH-1001 BIOS logo checkpoint on hardware, which entry 29 left untested, and compare it with the software-GPU result from entry 26.

#### Outcome:

`psx_bios.tpx` was rebuilt from `f77f3be` (589,772 bytes, CRC-32 `c9a05a30`, SHA-256 `e567fc16a41918330643460910348b8bc60518ad0a900756f44a95136695b7b5`, payload CRC-32 `bb3e91ef`). It replaced the entry 26 software-GPU image on the SD card and was verified on SD readback. On the unchanged entry 29 core (`2cf878cc`) the Gate 1 self-checks passed, and the run returned `0xb1051001` at stage `0x80011001` 8.28 s after stream start, with no stream overflow or HDMI underflow. Its telemetry matched the host checkpoint exactly: 27,870,497 guest instructions, 10,768 GPU words, 414 primitives, 63 uploads, 144 VBlanks, 158,497 DMA words, and PC `80047c2c`. The user saw a correct logo sequence and completion. The timing summary reported 6,347 ms to the logo, against entry 26's 5,866 ms with the software GPU. GPU time, including waits for the fabric, rose from 3,753 ms to 4,157 ms, and the display copy from 125 ms to 161 ms. The remaining CPU emulation stayed at about 1,851 ms, and VBlank 13 arrived at 1,453 ms instead of 1,510 ms. The fabric rasterizer is therefore about 11 percent slower than the 750 MHz software renderer on this workload. Together with entry 29's unchanged Spyro frame rate, this shows that the accelerator has not yet produced a speedup. The cause is untested; the working hypothesis is the rasterizer's multi-cycle per-pixel handling at 75 MHz with VRAM read-modify-write in DDR. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 30 with exactly six required sections, confirmed that 30 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user. Choose between profiling and pipelining the fabric rasterizer's per-pixel path and DDR access until it beats the software renderer, and returning effort to CPU emulation and JIT coverage, which dominates Spyro's wall time. The unused descriptor DMA, controller gameplay, semi-transparency, and display-mode work from entries 28 and 29 remain open.

#### Files Modified:

None.

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 31 COMMIT Unreleased 2026-09-29T09:59:53-07:00

#### Coming From:

Unreleased 972e343

#### Purpose:

Port GNU Lightning's RISC-V backend to RV32 and validate it under QEMU, as step 1 of the user-approved plan to replace the Tang-PSX dynamic recompiler with Lightrec.

#### Outcome:

Lightrec emits all code through GNU Lightning, whose RISC-V backend at the pinned submodule commit `a6bb2b5` is 64-bit only (TOOL-008). The port is kept as `third_party/patches/gnu-lightning-rv32.patch`. `tools/lightning_source.py` exports the pinned commit and applies the patch, and the submodule itself stays clean. The patch adds a 32-bit frame bound and 8-byte aligned double callee saves, word-sized loads, stores, and variadic save slots, and `LUI`+`ADDI` constants with the constant pool limited to RV64. It also adds word-relative extension shifts, 32-bit integer and floating-point conversions, and double to register-pair transfers through a per-function `allocai` slot. Double arguments follow the `ilp32d` rules read from GCC's generated assembly (TOOL-009): a register pair, the `a7` plus stack split, 8-byte aligned stack slots, and even pairs for variadic doubles. The RV32 instruction-size table is the element-wise maximum of sizes measured by the check suite in `GET_JIT_SIZE` mode and the upstream RV64 table. The patch also corrects the shared `jit_fallback.c` little-endian 32-bit `unldi_x` and `unsti_x`, which placed the high word first in the `movr_ww_d` pair contrary to Lightning's documentation. `tests/test_lightning_rv32.py` builds Lightning and its check driver with newlib, supported by the `tests/lightning_shim` stand-ins for `popen`, `dlsym`, `sysconf`, and `mmap`. It preprocesses each `.tst` file on the host, runs the 67 base tests with and without data buffers plus 11 C interop programs under qemu user mode, and compares the output with the `.ok` files. For 32-bit targets it replaces one `gen_cbit` constant that is undefined in C when `long` is 32 bits. The unmodified RV64 backend first passed 134 of 134 base checks. After the port, all 145 checks passed on RV64 and on RV32 `ilp32d`, and 142 of 145 passed on RV32 `ilp32`, the AE350 program ABI. Its three failures, `ccall`, `carg`, and `cva_list`, pass floating-point arguments to compiled C; the port passes those in FP registers while `ilp32` expects integer registers. Lightning generates identical code under both 32-bit ABIs and Lightrec passes no floating-point arguments, so the harness records these three as expected failures. No hardware build or deployment was part of this cycle. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff including TOOL-008 and TOOL-009, validated this entry as number 31 with exactly six required sections, confirmed that 31 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

With the user's go-ahead, start step 2 of the approved Lightrec plan. Build Lightrec bare-metal for RV32 `ilp32` with the threaded compiler off and an external code buffer, and connect it to `software/psx` machine and GTE through `lightrec_ops`. It must reproduce the exact SCPH-1001 logo telemetry and framebuffer SHA-256 `0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7` under `qemu-riscv32`. Then compare its executed-instruction cost with the current JIT before any hardware run.

#### Files Modified:

- tests/lightning_shim/dlfcn.h
- tests/lightning_shim/shim.c
- tests/lightning_shim/sys/mman.h
- tests/test_lightning_rv32.py
- third_party/patches/gnu-lightning-rv32.patch
- tools/lightning_source.py

#### Status:

- Build: PASS
- Deployment: N/A
- User Test: N/A

---

## 32 COMMIT Unreleased 2026-09-29T10:28:28-07:00

#### Coming From:

Unreleased 53fbdc8

#### Purpose:

Shorten the GNU Lightning RV32 patch test loop by building in parallel, finding the slow part of the RV64 run, and running all targets concurrently, in the three user-approved steps.

#### Outcome:

Before this cycle, `tests/test_lightning_rv32.py` recompiled the Lightning library for every C check program. A full run took 51 s on each RV32 target and 139 s on RV64, and the quick 67-test check took 6.7 s. The harness now compiles the library, shim, and check driver once per target as concurrent compiler jobs, and it compiles and links the 11 C programs against those objects concurrently. That cut a full RV32 run to 14 s and RV64 to 93 s. The new `--timings N` option reports the N slowest tests. No test took more than 2.6 s on either word size, and the RV64 build phases showed that the generated 80,058-line `cbit.c` alone took 85.6 s to compile at `-O2` (50.1 s without debug information, 40.5 s at `-O1`, and 4.4 s at `-O0`). As the user approved, only `cbit.c` is now compiled at `-O0`; it is test-side code, the Lightning library under test stays at `-O2`, and `cbit` passed at `-O0` on both RV64 and RV32. The new `--target all` option runs the three targets concurrently in separate work directories and prints each report whole, and `--measure-sizes` now requires a single target. This cycle also fixed a defect in the entry 31 harness: `--measure-sizes` read Lightning's code names after the temporary source tree had been deleted. With `--target all`, all three targets finished in 22.6 s with exit status 0. Results were unchanged at 145 of 145 checks on RV64 and RV32 `ilp32d`, and 142 of 145 on RV32 `ilp32` with the same three expected failures. Run alone, RV64 took 20.9 s, each RV32 target 7.8 s, and the quick check 5.2 s. The RV32 size table measured in `GET_JIT_SIZE` mode is byte-identical to the one in `third_party/patches/gnu-lightning-rv32.patch`. No hardware build or deployment was part of this cycle. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 32 with exactly six required sections, confirmed that 32 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

With the user's go-ahead, start step 2 of the approved Lightrec plan as recorded in entry 31. Validate Lightning patch changes with `tests/test_lightning_rv32.py --target all`.

#### Files Modified:

- tests/test_lightning_rv32.py

#### Status:

- Build: PASS
- Deployment: N/A
- User Test: N/A

---

## 33 COMMIT Unreleased 2026-09-29T11:12:26-07:00

#### Coming From:

Unreleased cd2cf2a

#### Purpose:

Build Lightrec bare-metal for the AE350 and connect it to the Tang-PSX machine as its R3000A core under qemu-riscv32, completing phases 1 to 3 of step 2 of the approved Lightrec plan.

#### Outcome:

Phase 1 builds Lightrec (`a7464cc`, unmodified) and the patched GNU Lightning as `liblightrec.a` for `rv32imafdc` `ilp32` with `tools/lightrec_build.py`, in about 3 s. The build uses the static `software/lightrec/lightrec-config.h` (no threaded compiler, TLSF-managed caller code buffer, upstream optimization defaults) and Lightning with `HAVE_MMAP=0`. Programs link it with newlib-nano and `software/lightrec/runtime.c`, which supplies the system calls over a static heap, `sysconf` for Lightning's cache flush, and two platform hooks for output and exit. `third_party/patches/gnu-lightning-rv32.patch` gains one `lib/lightning.c` fix: without mmap, `jit_emit` retried a too-small user code buffer forever and now returns NULL. The Lightning check suite still passes 145, 145, and 142 of 145 with the three expected failures, and `tests/lightrec_smoke_rv32.c` confirms that the small buffer is rejected and that Lightrec runs a MIPS program correctly when compiled and when interpreted. Phase 2 connects Lightrec through `software/lightrec/psx_lightrec.c`. RAM with three mirrors, BIOS, and scratchpad are direct maps, and I/O, the parallel port, and cache control go through the machine bus, which `software/psx/machine.c` now exports with device servicing and a RAM-write hook that invalidates code after DMA. GTE commands run on Lightrec's registers, whose layout matches `struct psx_gte`. After each run the machine's CPU state mirrors Lightrec's registers, so the event-driven VBlank detection works unchanged. The BIOS, whose cache-isolated stores all fell within the first 4 KiB, is covered by saving and restoring 64 KiB of low RAM around isolation. Phase 3 hardened the run loop against the directed test `tests/psx_lightrec_unit_rv32.c`, now run with the smoke test by `tests/test_lightrec_rv32.py`. That test found that an interrupt raised by an I/O write before a block-ending SYSCALL was entered after the syscall, which is now fixed. It also showed that the host must not execute a GTE command on interrupt entry, because Lightrec re-runs it on the handler's return (TOOL-010). Lightrec's counter now holds the low 31 bits of the machine cycle count, so run targets never wrap. Built natively for x86-64 as well, the test exposed two upstream Lightrec limits, left unpatched and documented in `software/lightrec/psx_lightrec.h` and TOOL-010. Cache isolation is honored only from uncached code, and a JR to a known kseg1 address from kseg0 RAM becomes a J that stays in kseg0. Under `qemu-riscv32`, `tests/psx_bios_lightrec_rv32.c` boots SCPH-1001 to the logo checkpoint with framebuffer SHA-256 `0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7`, 144 VBlanks, 10,768 GPU words, 414 primitives, 63 uploads, and 158,497 DMA words, matching the interpreter, in 0.9 s. It executes 30,182,928 guest instructions with no pattern accelerators, and the result is identical when the guest clock starts at 0, `0x7ff00000`, or `0xfff00000`. That harness has no committed runner yet. The existing `test_psx_bios`, `test_psx_bios_jit`, `test_psx_cdrom`, `test_psx_diag`, `test_psx_gpu`, `test_psx_gpu_accel`, and `test_psx_jit` tests pass (ASan tests run without `LD_PRELOAD`), and the `psx_bios`, `psx_perf`, and `psx_disc` hardware programs build. No hardware build or deployment was part of this cycle. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff including TOOL-010, validated this entry as number 33 with exactly six required sections, confirmed that 33 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Complete phase 4 of step 2 by adding a committed runner for `tests/psx_bios_lightrec_rv32.c` that checks the framebuffer hash and logo telemetry at the three starting cycle counts. Then compare Lightrec with the current JIT under QEMU as step 3 of the approved plan before any hardware run. Whether to carry a Lightrec patch for the JR-to-J segment defect is for the user to decide.

#### Files Modified:

- software/lightrec/lightrec-config.h
- software/lightrec/psx_lightrec.c
- software/lightrec/psx_lightrec.h
- software/lightrec/runtime.c
- software/lightrec/runtime.h
- software/psx/machine.c
- software/psx/machine.h
- tests/lightrec_smoke_rv32.c
- tests/psx_bios_lightrec_rv32.c
- tests/psx_lightrec_unit_rv32.c
- tests/test_lightrec_rv32.py
- third_party/patches/gnu-lightning-rv32.patch
- tools/lightrec_build.py

#### Status:

- Build: PASS
- Deployment: N/A
- User Test: N/A

---
