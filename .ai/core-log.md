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

## 34 COMMIT Unreleased 2026-09-29T11:25:37-07:00

#### Coming From:

Unreleased a87059e

#### Purpose:

Add the committed pass/fail gate for the SCPH-1001 logo checkpoint under Lightrec, completing phase 4 and with it step 2 of the approved Lightrec plan.

#### Outcome:

`tests/test_psx_bios_lightrec.py` builds `liblightrec.a` once with `tools/lightrec_build.py`, then builds and runs `tests/psx_bios_lightrec_rv32.c` under `qemu-riscv32` concurrently with the guest clock starting at 0, `0x7ff00000`, and `0xfff00000`. Each run must reproduce the exact logo telemetry (114,713 service calls, 30,182,928 guest instructions, 144 VBlanks, 10,768 GPU words, 414 primitives, 63 uploads, and 158,497 DMA words) and framebuffer SHA-256 `0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7`, must end with no unexpected Lightrec exit flags, and must report byte-identical statistics to the run starting at 0, including 438 interrupts, 6 system calls, 1,208 compiled blocks in 331,900 bytes of code, and 2,929,660 heap bytes. The gate passed in 4.8 s. Two temporary mutations of `software/lightrec/psx_lightrec.c`, both reverted, checked its sensitivity. Emptying the RAM restore after cache isolation stopped the BIOS before its first VBlank at the 400 million instruction limit, and the gate failed. Skipping code invalidation after DMA left the checkpoint exact because the BIOS never executes code that DMA overwrote, so that path remains covered only by `tests/psx_lightrec_unit_rv32.c` through `tests/test_lightrec_rv32.py`. The user chose to leave the upstream Lightrec JR-to-J segment defect unpatched, and it remains documented in `software/lightrec/psx_lightrec.h` and TOOL-010. No hardware build or deployment was part of this cycle. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 34 with exactly six required sections, confirmed that 34 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

With the user's go-ahead, start step 3 of the approved Lightrec plan by comparing Lightrec with the current JIT and the interpreter under QEMU on the same checkpoint for correctness, executed-instruction cost, code size, and memory, then move to the AE350 hardware integration in step 4. Run both `tests/test_lightrec_rv32.py` and `tests/test_psx_bios_lightrec.py` after any change to the Lightrec integration.

#### Files Modified:

- tests/test_psx_bios_lightrec.py

#### Status:

- Build: PASS
- Deployment: N/A
- User Test: N/A

---

## 35 COMMIT Unreleased 2026-09-29T11:53:48-07:00

#### Coming From:

Unreleased afe0293

#### Purpose:

Compare Lightrec with the current JIT and the interpreter on the SCPH-1001 logo checkpoint under QEMU for correctness, executed-instruction cost, code size, and memory, as step 3 of the approved Lightrec plan.

#### Outcome:

`tests/psx_bios_lightrec_rv32.c` became `tests/psx_bios_cores_rv32.c`, one harness for every core that shares the service loop, newlib-nano, and `software/lightrec/runtime.c`: `PSX_BIOS_LIGHTREC` selects Lightrec, and otherwise `psx_machine_run` runs the JIT, or the interpreter alone with the new `PSX_JIT_INTERPRET_ONLY` switch in `software/psx/jit.c`. The new `PSX_DISABLE_ACCEL` switch in `software/psx/machine.c` turns off the signature-checked BIOS loop accelerators, which Lightrec does not use. Neither switch is set by any hardware program. `tools/qemu_insn_profile.c` is a QEMU TCG plugin that counts executed RV32 instructions, loads, and stores per code address, and `tools/psx_core_compare.py` builds five cores for `rv32imafdc` `ilp32` at `-O2` with named objects and a link map, runs them concurrently to the checkpoint in about 8 s, verifies each result, and attributes the counts to code groups and functions. The Ubuntu `qemu-user` 10.2.1 package has no plugin support, so QEMU 10.2.1 was built locally with `--enable-plugins` against extracted glib development packages; the tool's docstring gives the configuration. Every core reproduced framebuffer SHA-256 `0b884450d8c8f3becc8ed4c9e7bdbd04ae0132640e1cdf48513dcd561eb47ae7`, the interpreter and JIT reproduced the 27,870,497-instruction accelerated checkpoint exactly, and without accelerators both executed an identical 30,133,834 instructions. Lightrec's CPU core (generated code, Lightrec and Lightning, and the glue) costs 14.8 RV32 instructions per guest instruction, against 157 to 160 for the JIT and 166 for the interpreter, because the JIT runs only 12.5 million of 30.1 million unaccelerated guest instructions in compiled code while its block source check and interpreter fallback cost more than compiled code saves. The first run showed Lightrec spending about 900 million instructions in newlib-nano's byte-loop `memset` and `memcpy`, mostly GNU Lightning clearing its node pools and liveness sets on each of 1,208 compiles, 12 whole-table invalidations when the BIOS leaves cache isolation, and the register copies in `psx_lightrec_run`, so `runtime.c` now supplies word-wise versions. With them the checkpoint costs 1,933 million RV32 instructions under Lightrec, against 2,353 million for the accelerated JIT that runs on hardware today, 2,389 million for the accelerated interpreter, and 6,119 million for the JIT without accelerators; 1,263 million of each is the software GPU's `draw_triangle`, which hardware now offloads to the fabric rasterizer. Lightrec's CPU core alone costs 447 million against 943 million for the accelerated JIT. Lightrec's image has 257 KB of host text against 48 KB, uses 2.9 MB of heap, and generated 332 KB of code, while the JIT generated 645 KB over five cache flushes. `software/lightrec/psx_lightrec.c` now counts cache isolations. QEMU models neither the AE350 caches nor DDR3 latency, so these counts are a cost proxy and the hardware effect of Lightrec's larger code and heap is unmeasured. `tests/test_lightrec_rv32.py`, `tests/test_psx_bios_lightrec.py` (unchanged statistics), `test_psx_bios_jit`, `test_psx_jit`, `test_psx_bios`, `test_psx_diag`, `test_psx_cdrom`, `test_psx_gpu`, and `test_psx_gpu_accel` pass, and the `psx_bios` and `psx_disc` hardware programs build. No hardware build or deployment was part of this cycle. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 35 with exactly six required sections, confirmed that 35 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

With the user's go-ahead, start step 4 of the approved Lightrec plan by running Lightrec on the AE350 inside a hardware program built with `software/lightrec/runtime.c` and the fabric GPU, first measuring the SCPH-1001 logo checkpoint against the JIT's hardware timing and then Spyro, whose frame rate is bound by CPU emulation. Rerun `tools/psx_core_compare.py` with a plugin-enabled `qemu-riscv32` after changes that affect CPU cost; the one built for this cycle lived in a session scratch directory, so rebuild it as the tool's docstring describes, and if glib headers are not installed, extract `libglib2.0-dev`, `libgio-2.0-dev`, and their `-dev` dependencies with `apt-get download` and `dpkg -x` into a local root and point `PKG_CONFIG_PATH` at it, since installing packages needs sudo. Run both `tests/test_lightrec_rv32.py` and `tests/test_psx_bios_lightrec.py` after any change to the Lightrec integration.

#### Files Modified:

- software/lightrec/psx_lightrec.c
- software/lightrec/psx_lightrec.h
- software/lightrec/runtime.c
- software/lightrec/runtime.h
- software/psx/jit.c
- software/psx/machine.c
- tests/psx_bios_cores_rv32.c
- tests/psx_bios_lightrec_rv32.c
- tests/test_psx_bios_lightrec.py
- tools/psx_core_compare.py
- tools/qemu_insn_profile.c

#### Status:

- Build: PASS
- Deployment: N/A
- User Test: N/A

---

## 36 COMMIT Unreleased 2026-09-29T17:08:12-07:00

#### Coming From:

Unreleased 75a0d08

#### Purpose:

Run Lightrec on the AE350 as step 4 of the approved Lightrec plan, first on the SCPH-1001 logo checkpoint against the JIT and then on Spyro the Dragon.

#### Outcome:

Lightrec runs correctly on the AE350 for the SCPH-1001 logo but is slower than the JIT there, and it fails to boot Spyro, so it is not yet a replacement. `scripts/build-programs.sh` builds `psx_bios_lightrec` and `psx_disc_lightrec` from the existing programs with `PSX_LIGHTREC`, linking `build/lightrec/liblightrec.a`, `software/lightrec` and newlib-nano, and the new `software/lightrec/tpx_platform.c` routes runtime output to the loader log and turns a runtime exit into failure `0x4c52xxxx` at stage `0x800fbad0`. Lightning's own flush compiles to nothing on this bare-metal target, so every emission reaches instruction fetch only through Lightrec's `code_inv` hook, which executes `fence rw,rw` and `fence.i` as the JIT does. Building the library under `build/` exposed that `git apply` had silently skipped the whole RV32 patch whenever the export lay inside the Tang-PSX work tree (TOOL-011); `tools/lightning_source.py` now stops repository discovery at the destination, and the Lightning suite still passes 145, 145 and 142 of 145 with the three expected failures, as do `tests/test_lightrec_rv32.py` and `tests/test_psx_bios_lightrec.py`. All hardware runs used the entry 37 VGA cores, whose CPU, DDR3 and GPU logic is unchanged from entry 29. `psx_bios_lightrec.tpx` (payload 809,628 bytes, CRC-32 `6521c4de`) returned `0xb1051001` with telemetry and Lightrec statistics exactly matching QEMU (30,182,928 instructions, 144 VBlanks, 1,208 blocks in 331,900 bytes, 2,929,660 heap bytes, 438 interrupts) in 12,693 ms, against 6,342 ms for the JIT `psx_bios.tpx` (payload 589,708 bytes, CRC-32 `e5bbc1b5`), which reproduced entry 30. Nearly all of the gap is before VBlank 13 (7,575 ms against 1,450 ms), where Lightrec compiles most blocks and the JIT uses its BIOS loop accelerators; from VBlank 13 to the logo Lightrec took 5,118 ms against 4,892 ms, with the fabric GPU taking about 4.2 s in both. The unverified working hypothesis for the slow start is cache misses in Lightrec's large compiler and heap through the 75 MHz RAM bridge. Running the JIT after Lightrec without reloading the core hung at startup because `psx_gpu_reset` waited for the fabric's completed-primitive count to match the new program's zero while it still held 414 from the previous program, a defect present since entry 29; it now waits for the fabric to go idle and resets it, and `tests/test_psx_gpu_accel.py` and `tests/test_psx_bios.py` pass. On Spyro, `psx_disc_lightrec.tpx` (payload 810,432 bytes, CRC-32 `1519b348`) stopped at the PlayStation logo after 13 sectors: the BIOS read the first sector of `SCUS_942.28` (LBA 53875) into its buffer, but the kernel `read()` in its header loader at `0xbfc03c90` reported fewer than 2,048 bytes, so it called `SystemErrorBootOrDiskFailure('B', 0x38A)` and looped. The new `tests/psx_disc_cores_rv32.c` reproduces this exactly under `qemu-riscv32` with Lightrec, while its JIT build continues to 1,002 sectors like the hardware, and its CD-ROM command trace shows both cores issuing identical commands up to that point. The JIT `psx_disc.tpx` (CRC-32 `2c153712`, byte-identical to the build before this cycle) ran Spyro on hardware to 1,002 sectors and VBlank 2,530 after 376 s, about 5 to 6 VBlanks per second through the intro, which the user watched. The user test failed because Lightrec cannot boot Spyro. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff including TOOL-011, validated this entry as number 36 with exactly six required sections, confirmed that 36 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user whether to continue Lightrec. Continuing means finding why the kernel's CD `read()` reports a short read under Lightrec, using `tests/psx_disc_cores_rv32.c` (built like `tests/test_psx_bios_lightrec.py` with `DISC_PATH`, `SECONDS` and optionally `CD_TRACE` or `RAM_DUMP` defined) to compare the kernel's event and interrupt handling between the cores, then comparing Spyro's steady-state frame rate against the JIT on hardware, since the logo checkpoint mostly measures Lightrec's slow start. Otherwise effort returns to the JIT, whose Spyro intro runs at about a tenth of real time with CPU emulation dominant.

#### Files Modified:

- scripts/build-programs.sh
- software/lightrec/tpx_platform.c
- software/lightrec/tpx_platform.h
- software/programs/psx_bios/main.c
- software/programs/psx_disc/main.c
- software/psx/gpu.c
- tests/psx_disc_cores_rv32.c
- tools/lightning_source.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: FAIL

---

## 37 COMMIT Unreleased 2026-09-29T17:14:40-07:00

#### Coming From:

Unreleased 75a0d08

#### Purpose:

Add Digilent PmodVGA output across the dock's two PMOD sockets, showing the same picture as HDMI, with a runtime-selectable module placement and test pattern that separate a misplaced module from a fault.

#### Outcome:

Gate 1 now drives a Digilent PmodVGA (BRD-005) as well as HDMI, and the user's Dell E773c CRT shows the same picture from power-on. The new `gateware/vga_output.py` observes the stream the Gowin HDMI PHY receives in the 25 MHz pixel domain and drives the top four bits of each colour, black outside the active area, and active-low syncs through two registered stages onto the 16 PMOD pins at LVCMOS33. A four-bit mode at debug address `0x210`, set with `tools/ae350_run.py vga`, swaps the sockets, swaps the rows, selects linear instead of interleaved pin numbering, or replaces the picture with colour bars and 16-step ramps that do not depend on DDR3; the debug ABI is now `0x00020003`. `gateware/sim/test_vga_output.py` drives the module from LiteX's timing generator and checks every pin in all 16 modes over 12,800 pixel clocks against an independent model, and it failed as intended for ignored row swaps, missing blanking, wrong source bits, a ramp bit-order error, sync polarity, and inverted interleaving. The first core assumed LiteX's linear PMOD order; its placement option 2 met timing and was deployed, but no placement produced a picture. TangCore's constraints put one of two controllers on each of IO0/2/4/6 and IO1/3/5/7, indicating that Sipeed's IO numbering interleaves the rows (BRD-004), so interleaved numbering became the default with linear kept selectable. Only placement option 3 of that build met timing; on it the test pattern appeared with J1 on the socket LiteX calls `pmod1`, beside the HDMI port, and the framebuffer on the CRT matched HDMI. The final build therefore makes `pmod1` socket a, so mode 0 is correct at power-on. Again only placement option 3 met all setup and hold timing, with Fmax 89.593 MHz for the 75 MHz `sys_clk`, 120.722 MHz for the 100 MHz `ddr_clk`, 57.658 MHz for the 50 MHz board clock, and 136.495 MHz for the 25 MHz pixel clock; options 1, 2 and 4 failed `sys_clk` or `ddr_clk` setup, continuing the one-in-four closure seen since entry 29. Its 5,018,822-byte image, SHA-256 `c26a23205104f0f95ccf180a536c4f365a27a52bf678fc23befd212525654ba8` and CRC-32 `c0ad9d23`, was installed as `cores/console138k/tang-psx.bin` with verified SD readback; the TangCore menu shows at most 14 characters of a name, so the earlier `tang-psx-gate1-vga.bin` looked like `tang-psx-gate1.bin` and was removed at the user's request, and the entry 29 core remains as `tang-psx-gate1.bin`. On it the Gate 1 self-checks passed (stage `0x80000001`, features `0xff`), the CRT showed the image at power-on with no command, and the Lightrec and Spyro runs of entry 36 ran. The user accepted the result. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff including BRD-004 and BRD-005, validated this entry as number 37 with exactly six required sections, confirmed that 37 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

No VGA work remains; the pattern's bar order and ramp steps were not separately confirmed and can be checked with `tools/ae350_run.py vga --pattern` if colour accuracy matters. The open project decision is the one recorded in entry 36, whether to continue Lightrec or return to the JIT.

#### Files Modified:

- README.md
- gateware/ae350_gate1.py
- gateware/sim/test_vga_output.py
- gateware/vga_output.py
- tools/ae350_run.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 38 COMMIT Unreleased 2026-09-29T18:36:05-07:00

#### Coming From:

Unreleased 35b9840

#### Purpose:

Find and fix why Lightrec could not boot Spyro on the AE350, as the user chose to continue Lightrec after entry 36, and compare it with the JIT on Spyro.

#### Outcome:

Two GNU Lightning RISC-V backend defects caused the failure, and with both fixed Lightrec boots Spyro to its main menu on the AE350 and runs its intro about 2.6 times as fast as the JIT, so the user chose Lightrec as the CPU core going forward. `tests/psx_disc_cores_rv32.c` now also builds natively, and native x86-64 Lightrec with the same glue and machine model booted Spyro, as did Lightrec's own interpreter (`lightrec_run_interpreter`) on RV32, whose progress matched native exactly, which placed the fault in RV32 code generation. The harness's new `RUN_TRACE`, `FINE_FROM` and `DUMP_AT` options compared compiled and interpreted RV32 runs block by block, and the first divergence was the SCPH-1001 shell's `sltiu at, a0, 0x1000` at `0x80055fb0`, which took the wrong branch so that the BIOS's EXE header read reported failure. GNU Lightning's `_lti`, `_lti_u`, `_gti` and `_gti_u` load an immediate that does not fit 12 bits into the destination and compare against an unset temporary, and with that fixed a Lightrec block 17 emulated seconds into Spyro failed the `_Btype` assertion because conditional branches reach only 2 KiB; both defects are also present in the canonical GNU Lightning master `7965700`, which is still RV64-only (TOOL-012). `third_party/patches/gnu-lightning-rv32.patch` now loads the immediate into the temporary and adds `_bcc`, which emits a single B-type branch only for a known target in range and otherwise the inverted branch over a `JAL`; register conditional branches are 8 bytes in both size tables and every other branch allows 4 more bytes, and 4 more again for an immediate. The Lightning suite passes 145, 145 and 142 of 145 with the three expected failures, and `tests/test_lightrec_rv32.py` passes. The fix changed the SCPH-1001 logo checkpoint under Lightrec to 30,182,705 instructions in 1,213 blocks and 348,660 bytes of code; Lightrec's interpreter on RV32 gives the same 30,182,705 instructions, runs, interrupts and I/O counts with the standard framebuffer SHA-256, so the 30,182,928 recorded in entries 34 to 36 came from the compare defect, and `tests/test_psx_bios_lightrec.py` now expects the corrected count. Under `qemu-riscv32` Spyro with compiled Lightrec matched native x86-64 at every emulated second for 60 seconds. On the entry 37 core, `psx_disc_lightrec.tpx` (payload 810,400 bytes, CRC-32 `004b0b95`) booted Spyro through the former stall to its main menu; from 1,002 sectors on it advanced about 14.6 VBlanks per second against the JIT's 5.7 in entry 36, reaching VBlank 2,386 after 152 s against the JIT's 2,430 after 356 s. One Tang-Control `FPGA debug request timed out` interrupted the sampling script without affecting the run. The rebuilt `psx_bios_lightrec.tpx` (CRC-32 `28aace56`) is on the SD card but was not run on hardware. The user accepted the result. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff including TOOL-012, validated this entry as number 38 with exactly six required sections, confirmed that 38 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user; Lightrec is now the CPU core, but gameplay is not yet possible because Gate 1 ties Tang-Control's controller inputs to zero, so the next milestone for play is routing the BL616 controller state to the emulated digital pad. Other open work is making the Lightrec programs the default builds, rerunning the SCPH-1001 logo timing with the fixed library, profiling Lightrec's slow start from entry 36, committing a runner for `tests/psx_disc_cores_rv32.c` (built like `tests/test_psx_bios_lightrec.py` with `DISC_PATH` and `SECONDS`, or natively with the host compiler), and offering both GNU Lightning fixes upstream.

#### Files Modified:

- tests/psx_disc_cores_rv32.c
- tests/test_psx_bios_lightrec.py
- third_party/patches/gnu-lightning-rv32.patch

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 39 COMMIT Unreleased 2026-09-29T18:53:35-07:00

#### Coming From:

Unreleased 62868af

#### Purpose:

Measure where the fabric GPU path spends its time during Spyro's intro under Lightrec, as step 1 of the user-approved GPU optimization plan, without changing emulation.

#### Outcome:

The fabric rasterizer's pixel rate is the dominant GPU cost, confirming it as the first optimization target. `software/psx/gpu.c` now accumulates, for `PSX_GPU_ACCEL` builds only, the AE350 cycles spent in `accel_push` and the part of them stalled on a full fabric FIFO, in drain waits, and in D-cache flushes, with counts of words pushed, primitives, and fabric-written pixels summed across the driver's fabric resets, and exposes them through `psx_gpu_accel_stats`; `software/programs/psx_disc/main.c` logs them every 5 s, and `tests/test_psx_gpu_accel.py` renames the new function in its reference build. The fabric comparison still matches in 33,271,453 cycles and the software GPU and BIOS checkpoints pass. On the entry 37 core, `psx_disc_lightrec.tpx` (payload 812,728 bytes, CRC-32 `ce6148a9`) ran Spyro's intro from VBlank 1,715 to 2,676 in 67.0 s, 14.3 VBlanks per second against 14.6 uninstrumented in entry 38. Of that time the AE350 spent 36.8 s in GPU work: 22.4 s stalled on a full fabric FIFO, 6.5 s pushing 20.9 million words at about 313 ns each (48 words for each of 436,000 primitives), 0.1 s draining and flushing, and 7.8 s parsing GP0 and setting up primitives. CPU emulation outside the GPU took 19.2 s, the display copy 8.8 s, and the rest 2.2 s. The fabric wrote 106 million pixels, about 110,000 per VBlank, so even if continuously busy it averaged at most about 1.6 million pixels per second, about 47 cycles of its 75 MHz clock per pixel, against a best case of about 12 cycles in its state machine, which places most of the cost in DDR3 misses of its two-line cache; full speed at this load needs well over 6.6 million pixels per second. No gateware changed and the user test is not applicable to this measurement. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 39 with exactly six required sections, confirmed that 39 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Design step 2 for user approval: a pipelined fabric rasterizer that starts a pixel on most clocks, with an on-chip CLUT and a multi-line block-RAM texture cache, bit-exact with the current one under `tests/test_psx_gpu_accel.py` and built to close timing. Then reduce the 48-word feed by moving triangle setup into the fabric and batching submission, and later consider dedicated SDRAM VRAM with hardware scanout. Leave the CPU emulation unchanged until the GPU path is optimized, as the user directed.

#### Files Modified:

- software/programs/psx_disc/main.c
- software/psx/gpu.c
- software/psx/gpu.h
- tests/test_psx_gpu_accel.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: N/A

---

## 40 COMMIT Unreleased 2026-09-29T19:10:47-07:00

#### Coming From:

Unreleased 404a3fe

#### Purpose:

Split the AE350's CPU-emulation time under Lightrec during Spyro's intro, as the user requested, without changing Lightrec or emulation.

#### Outcome:

Lightrec's own execution is about four fifths of CPU-emulation time, and our glue around it most of the rest. `software/lightrec/psx_lightrec.c` now accumulates host cycles for all of `psx_lightrec_run`, for `lightrec_execute`, and inside it for I/O reads, I/O writes, GTE commands and DMA code invalidation, plus `psx_machine_service`, reported through `struct psx_lightrec_stats` (zero on non-RISC-V hosts); `software/programs/psx_disc/main.c` logs them every 5 s in Lightrec builds. `tests/test_lightrec_rv32.py` and `tests/test_psx_bios_lightrec.py` pass with unchanged telemetry, although generated code shrank by 8 bytes to 348,652 because the glue's callback addresses moved. On the entry 37 core, `psx_disc_lightrec.tpx` (payload 815,008 bytes, CRC-32 `2da73e3a`) ran Spyro's intro from VBlank 1,687 to 2,633 in 67.0 s, with 56.0 s in CPU emulation including 36.1 s of GPU work, leaving 19.9 s, about 1.26 s per emulated second, against 1.20 in entry 39. Of that, `lightrec_execute` outside its callbacks took about 16.2 s, 17.2 ms per VBlank and alone above the 16.7 ms of a real-time VBlank, with only 62 blocks compiled in the window; GTE commands took 1.1 s, I/O reads 0.2 s, device service 0.4 s and DMA invalidation nothing after loading. The loop around Lightrec took about 3.0 s: 1.1 s inside `psx_lightrec_run` outside execute and service, and 1.9 s between the program's timer and the function's own, which with about 2,200 calls per VBlank is roughly 1,000 AE350 cycles per call and suggests instruction-cache misses on the glue after Lightrec code runs. The components exceed the total by about 1 s because some GPU time is recorded outside the I/O-write timer. `mcache_ctl` reads `0x3` (entry 18), so the A25's instruction and data prefetch (AE350-002 bits 9 and 10) are disabled. No gateware changed and the user test is not applicable to this measurement. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 40 with exactly six required sections, confirmed that 40 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Propose to the user a hardware run with the A25 instruction and data prefetch enabled, which changes neither Lightrec nor emulation results, measured on the same Spyro window. Reducing the glue's per-call cost without changing emulation timing, and optimizing the software GTE bit-exactly, are further CPU-side options; GPU step 2a from entry 39 remains the next GPU work.

#### Files Modified:

- software/lightrec/psx_lightrec.c
- software/lightrec/psx_lightrec.h
- software/programs/psx_disc/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: N/A

---

## 41 COMMIT Unreleased 2026-09-29T19:14:47-07:00

#### Coming From:

Unreleased d116d18

#### Purpose:

Test whether enabling the A25's hardware instruction and data prefetch speeds up CPU emulation, as the user approved, without changing Lightrec or emulation.

#### Outcome:

This board's A25 has no hardware cache prefetch, so the experiment could not run. A `psx_disc_lightrec.tpx` build (payload 815,064 bytes, CRC-32 `40f526dc`) set `mcache_ctl` bits 9 and 10 with `csrs` at startup and logged the register as read back; on the entry 37 core it logged `mcache_ctl 3`, so both prefetch enables read as zero and the run used the existing cache settings (AE350-008). The change was reverted and no program source changed. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff including AE350-008, validated this entry as number 41 with exactly six required sections, confirmed that 41 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user. CPU-side miss cost can only be reduced through a faster DDR3 path for the AE350 or better locality, and the glue's per-call overhead and the software GTE remain the other CPU-side options; GPU step 2a from entry 39 remains the next GPU work.

#### Files Modified:

None.

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: N/A

---

## 42 COMMIT Unreleased 2026-09-29T19:22:30-07:00

#### Coming From:

Unreleased 65a944f

#### Purpose:

Measure the cost of an AE350 cache miss to DDR3, as the user approved, to size the memory path as a lever for CPU emulation speed.

#### Outcome:

An AE350 D-cache miss to DDR3 costs about 570 core cycles, making memory latency a major lever for CPU speed that needs no change to Lightrec or emulation. The new `software/programs/memlat` flushes the D-cache and then times, in 750 MHz core cycles per access, dependent loads along a random single cycle of 32-byte lines, consecutive independent line loads, and one store per line. Its image (payload 1,082 bytes, CRC-32 `0c088000`) ran on the entry 37 core and returned `0x3e3a7001`, reporting 3.2 cycles for 16 KiB, 568 for 256 KiB and 570 for 4 MiB of dependent loads, 552 per line for consecutive independent loads, which shows that misses do not overlap, and 637 per line for stores including write-back (AE350-009). That is about 760 ns or 57 cycles of the 75 MHz system clock per miss, of which the 64-bit RAM port needs four to move a line, so most of it is latency in the RAM bridge, burst converter, 75-to-100 MHz crossing, arbiters and Gowin controller. With the instruction counts of entry 35, misses plausibly account for a large share of Lightrec's 17.2 ms per VBlank from entry 40 and of the glue's roughly 1,000 cycles per call, but that share was not measured. No gateware changed and the user test is not applicable to this measurement. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff including AE350-009, validated this entry as number 42 with exactly six required sections, confirmed that 42 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user. The recommended next step is to measure where the roughly 57 system cycles of a miss are spent along the AE350 RAM path, by simulation or latency counters, then decide between trimming that path and adding an FPGA-side L2 cache in block RAM on the AE350 RAM port; GPU step 2a from entry 39 would share the findings about the memory path.

#### Files Modified:

- software/programs/memlat/main.c

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: N/A

---

## 43 COMMIT Unreleased 2026-09-29T19:33:20-07:00

#### Coming From:

Unreleased 8a4c80d

#### Purpose:

Simulate the AE350's DDR3 read path stage by stage, as the user requested, to find where the roughly 57 system cycles of a cache miss measured in entry 42 are spent.

#### Outcome:

Our gateware accounts for 14 to 23 of the roughly 57 system cycles of a miss, and the rest lies in the Gowin DDR3 controller. The new `gateware/sim/test_ae350_memory_latency.py` builds the path from Gate 1's own modules (LiteX's bursting `AHB2Wishbone`, `WishboneRegisterSlice`, `BurstWishbone2Native`, `DDR3RWArbiter` and `DDR3PortArbiter` with idle GPU and video clients, and the 75-to-100 MHz `LiteDRAMNativePortCDC` with GowinDDR3's read-data Buffer), restates `gowin_ddr3_native.sv` in Migen, and models the encrypted controller as an always-ready port with a fixed read latency L in DDR clocks, with clock periods in the true 4:3 ratio. For a 64-bit WRAP4 line-fill burst, the assumed AE350 form, the first beat returns 14 + 0.75·L system cycles after the AHB address phase and the last 23 + 0.75·L; at L = 0 the command reaches the controller after 8.5 cycles through the bridge, register slice, burst converter, both arbiters and the command crossing, the data returns in 5.5 more through the adapter, the read crossing and the Buffer, and beats 2 to 4 take 3 cycles each through the register slice. Matched to the measured miss, the controller and any contention from HDMI scanout take about 34 to 43 system cycles, a read latency of about 45 to 57 DDR clocks or 450 to 570 ns, depending on whether the A25 restarts on the first beat or waits for the line; the controller's latency was not measured directly. Removing avoidable stages, pipelining the burst beats and avoiding the clock crossings could save about 10 to 13 cycles, while an FPGA-side cache in block RAM could serve hits in an estimated 6 to 8. The user test is not applicable to this simulation. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 43 with exactly six required sections, confirmed that 43 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user whether to build a hardware counter of the Gowin controller's read latency in the native adapter on its own or with a prototype L2 cache on the AE350 RAM port; GPU step 2a from entry 39 remains queued.

#### Files Modified:

- gateware/sim/test_ae350_memory_latency.py

#### Status:

- Build: N/A
- Deployment: N/A
- User Test: N/A

---

## 44 COMMIT Unreleased 2026-09-29T21:45:42-07:00

#### Coming From:

Unreleased 825ce58

#### Purpose:

Add a fabric L2 cache on the AE350 RAM port with a hardware counter of the Gowin controller's read latency, as the user chose after entry 43, and measure both on hardware.

#### Outcome:

A 128 KiB fabric L2 cuts an AE350 miss that hits in it from about 570 to 240 core cycles and made Spyro's CPU-bound disc loading up to about three times as fast. The new `gateware/l2_cache.py` sits between `Gate1RAMBridge` and the DDR3 arbiters: 4,096 direct-mapped 32-byte lines in block RAM, write-through with updates to present lines, with `0x7fe00000`-`0x7fffffff` (VRAM and the HDMI framebuffer, which the fabric GPU writes) always bypassed, and an enable at debug address `0x218` under which writes still update present lines so it stays coherent. `gateware/sim/test_l2_cache.py` checks it against a reference memory with byte-masked writes, conflicting lines, enable toggles and an external writer in the bypass range; mutations removing write updates, whole-line writes, a narrowed tag compare and caching of the bypass range all failed it, while routing bypass reads through the lookup alone does not, because bypass lines are never filled. `gowin_ddr3_native.sv` now times each read from issue to returned data, `gateware/sim/gowin_ddr3_native_tb.sv` checks the counters, and debug addresses `0x220`-`0x238` publish L2 hits, misses, bypass reads and writes and the latency count, sum and maximum; the debug ABI is `0x00020004`, and `tools/ae350_run.py l2` switches and reports them. `gateware/sim/test_ae350_memory_latency.py --l2` predicts a hit line fill of 16 system cycles. Of four placements only option 3 met timing, with Fmax 77.919 MHz for the 75 MHz `sys_clk`, 109.368 MHz for the 100 MHz `ddr_clk`, 74.700 MHz for the 50 MHz board clock and 146.735 MHz for the pixel clock, while options 1, 2 and 4 failed `sys_clk` setup (TNS -33.556, -5.029 and -192.418 ns); block RAM rose from 121 to 189 of 340. The 5,079,548-byte image (SHA-256 `2eeabcbb74878c38c51d20741fe45ae155e287553895ed584806989a5dc62b48`, CRC-32 `f029749e`) replaced `cores/console138k/tang-psx.bin` with verified readback, and its self-checks passed. The extended `memlat` (payload CRC-32 `dde8e023`) measured 240.0 core cycles for 64 KiB and 96 KiB with the L2 on and 569.6 to 569.7 with it off, about 570 for larger regions either way, and about 1 percent slower streaming with it on (AE350-010). The controller's read latency averaged 27.1 to 28.3 DDR clocks with a maximum of 60 to 70 (DDR3-005), lower than entry 43 inferred, leaving about 13 system cycles of a miss in the AE350 and in waits behind other clients; the 32-bit sum wraps after about 150 million reads. On Spyro with the L2 on, against entry 40's run of the same program on the previous core, the intro ran at 15.0 against 14.1 VBlanks per second with CPU emulation outside the GPU at 17.8 against 21.0 ms per VBlank and Lightrec's own execution at 15.5 against 17.4 ms, while the GPU stall still dominated; the boot reached VBlank 1,700 in 96 s against 118 s, and the disc-loading stretch from VBlank 650 to 1,000 took 5.0 s against 14.8 s. The user judged it about 1.5 times as fast and skipped a same-core L2-off run. A reset during that run hung the next program in `psx_gpu_reset`, because the reset had left the fabric holding half a descriptor that never drains; the wait for idle now gives up after about 100,000 polls before resetting the fabric, and the four GPU programs were rebuilt and uploaded (Lightrec disc payload CRC-32 `56a0fa69`). The user also wired a Raspberry Pi Pico 2 running JTAGprobe to the FPGA module's 8-pin JTAG + UART connector, whose pinout the module schematic gives (BRD-006), leaving its UART and 5 V pins unconnected; OpenOCD 0.12.0 found the Gowin TAP `0x0001081B`. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff including BRD-006, AE350-010 and DDR3-005, validated this entry as number 44 with exactly six required sections, confirmed that 44 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user. The L2's hit cost could fall further by streaming the four burst beats without the register slice's three cycles each, and its size or associativity could be revisited, but the dominant cost in the intro remains the fabric rasterizer stall, so GPU step 2a from entry 39 is the recommended next work, with the display copy and the 48-word feed after it. Confirm the timeout path of `psx_gpu_reset` on hardware by resetting the AE350 during a Spyro run and starting it again.

#### Files Modified:

- README.md
- gateware/ae350_gate1.py
- gateware/ddr3_vendor/gowin_ddr3_native.sv
- gateware/gowin_ddr3.py
- gateware/l2_cache.py
- gateware/sim/gowin_ddr3_native_tb.sv
- gateware/sim/test_ae350_memory_latency.py
- gateware/sim/test_l2_cache.py
- software/programs/memlat/main.c
- software/psx/gpu.c
- tools/ae350_run.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 45 COMMIT Unreleased 2026-09-29T22:49:37-07:00

#### Coming From:

Unreleased 582be08

#### Purpose:

Make the fabric rasterizer step to the next pixel in one clock, the user-approved revision of GPU step 2a after a replay of Spyro's GPU traffic showed stepping, not DDR3 misses, to be its main cost.

#### Outcome:

The rasterizer now steps in one clock instead of four, which made the SCPH-1001 logo 1.85 times as fast as entry 36 and Spyro's intro about 9 percent faster than with the L2 alone. `tests/psx_disc_cores_rv32.c` gained `GPU_TRACE`, which records a native build's GP0 and GP1 writes, GPUREAD reads and VBlanks; the native Lightrec harness recorded Spyro's first 2,519 VBlanks (7,540,496 GP0 words) in 2.3 s. `tests/psx_gpu_accel_diff.cpp` gained a replay mode that draws in software up to a chosen VBlank, draws with the Verilated rasterizer after it and compares VRAM at every VBlank, a `READ_LATENCY` memory model and a `STATE_HISTOGRAM` report, selected by `--trace`, `--first`, `--last`, `--read-latency` and `--histogram` in `tests/test_psx_gpu_accel.py`; the default random check is unchanged and its watchdog now allows 100 million clocks. On the random primitives at a 30-clock read latency the rasterizer took 67.3 clocks per pixel, 61 percent of them waiting for reads, while on Spyro's intro from VBlank 1,700 to 1,760 it took 15.7 clocks per pixel, 108,000 pixels per VBlank, with 59 percent of its clocks in the four stepping states, 33 percent of them stepping over bounding-box pixels outside the triangle, and 19 percent waiting for reads at 0.1 reads per pixel. The texture and CLUT caches of entry 39 were therefore deferred. `gateware/gpu_rasterizer.sv` now forms `rx - area` and `ry - area` per triangle, computes each next remainder with one add and a select and each next quotient from two parallel adds, and commits a step in the clock that finishes the pixel, so `ADVANCE`, `ATTR_NORM` and `ATTR_COMMIT` are gone; the replay then took 8.78 clocks per pixel and the random check 16.26 at a one-clock latency, both matching exactly. The first build failed on every placement (best `sys_clk` 69.090 MHz) because synthesis put `attr_rx_ma` and `attr_ry_ma` in LUT RAM and a marginal path ran from the DDR3 write-data crossing's full flag through both arbiters into the rasterizer. Keeping those arrays in registers, narrowing the remainder arithmetic to 27 bits (coordinates are 11-bit signed plus an 11-bit signed offset, so area is below 2^25 and the result stays exact), and a registered write-data Buffer before the crossing in `gateware/gowin_ddr3.py` let placement option 3 meet timing, with Fmax 75.660 MHz for the 75 MHz `sys_clk`, 115.523 MHz for `ddr_clk`, 72.220 MHz for the 50 MHz board clock and 134.076 MHz for the pixel clock, while options 1 and 4 failed `sys_clk` and `ddr_clk` setup and the user cancelled option 2; logic rose from 16 to 20 percent (27,444 LUTs). Its 5,127,164-byte image (SHA-256 `6dbe2e37a3220740ba9b8258eed3f5cd6f019ee08c009c37e4576be3c4c0c822`, CRC-32 `224b386a`) replaced `cores/console138k/tang-psx.bin` with verified readback, and its self-checks passed. The fabric `psx_bios.tpx` (payload CRC-32 `ce2c7eaf`) reproduced the exact logo telemetry in 3,422 ms, with 1,545 ms in the GPU, against 6,342 and 4,158 ms in entry 36. Spyro's intro under Lightrec ran at 16.4 VBlanks per second, 60.8 ms per VBlank with 31.1 ms in GPU work of which 16.1 ms was waiting for the fabric, against 15.0 VBlanks per second and 22.3 ms of waiting with the L2 alone and 14.1 and 25.1 ms with neither, and the boot reached VBlank 1,700 in 86 s against 96 and 118 s; the waiting fell less than the replay predicted, because on hardware the rasterizer's reads and flush writes also wait on the arbiters and other clients. The user accepted the result. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 45 with exactly six required sections, confirmed that 45 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

Reevaluate with the user. The `sys_clk` margin is 0.9 percent, so the next GPU change should restore slack. Remaining GPU levers are decoupling the stepper from pixel processing so that outside-triangle steps overlap other work, the texture and CLUT caches for the read waits, a deeper descriptor FIFO or batched feed for the 48 words per primitive, and hardware scanout for the 9.3 ms display copy; the replay also cannot model arbiter contention, so hardware measurements remain the arbiter of each change.

#### Files Modified:

- gateware/gowin_ddr3.py
- gateware/gpu_rasterizer.sv
- tests/psx_disc_cores_rv32.c
- tests/psx_gpu_accel_diff.cpp
- tests/test_psx_gpu_accel.py

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---

## 46 COMMIT Unreleased 2026-09-29T23:46:39-07:00

#### Coming From:

Unreleased 97c061c

#### Purpose:

Restore the Gate 1 core's `sys_clk` setup slack before any further GPU work, as the user approved after entry 45 shipped a core with a 0.9 percent margin.

#### Outcome:

The cycle set out to restore `sys_clk` setup slack and ended by reverting every change it made, with the finding that per-path work cannot buy closure on this device. Three changes were implemented and each was verified against its own functional gate. In `gateware/l2_cache.py` the write data is presented to the data block RAM unconditionally with only the write enable gated by the tag compare, and the compare result and the line are registered in the lookup state with a new decide state that consumes them, which removed the path entry 45 measured as its worst, in which the tag block RAM, a carry-chain equality and a 256-bit write-data mux fed the data block RAM write port; the cost is one system cycle, so a hit now returns three cycles after the command instead of two and a miss spends two lookup cycles before the DDR3 read. In `gateware/gpu_rasterizer.sv` the rectangle end-of-column and end-of-row conditions became registered flags following the `x_last` and `y_last` idiom the file already uses for triangles, which removed the path from `rect_row` through the 10-bit row compare into the 32-bit `current_y` increment. `gateware/sim/test_l2_cache.py` passed with unchanged counters, 2393 reads of which 271 hit, 1202 missed and 920 bypassed, plus 1607 writes, and `tests/test_psx_gpu_accel.py` matched 40000 random polygons and rectangles in the same 19639952 cycles after the second and third changes, so none of them regressed behaviour, and each did remove the path it targeted, with the tag block RAM leaving the worst-path list entirely. Timing nevertheless did not close. Four build sweeps were run, route option 1 with the first change alone, with the first two, and with all three, and route option 2 with all three: thirteen placement builds in all, whose best result was route option 1 at placement 3 with a `sys_clk` Fmax of 74.321 MHz against the 75.000 MHz constraint and a setup TNS of -0.228 ns over two endpoints. Route option 2 was uniformly worse and reached only 69.498 MHz at placement 3 with -22.432 ns over 45 endpoints, so the user rejected it and placement option 0 as well; placement 2 stalled in routing on both attempts, as it had in entry 45's cycle, and was killed both times. The worst `sys_clk` path at route option 1 placement 3 ran from the arbiter grant, fanout 102, through two LUTs into the reset input of the accelerator's read-data register with 11.6 of its 13.7 ns in routing, so the shortfall is a congestion and placement artefact rather than logic depth: nets moved about 1 ns between builds of identical logic and the worst path moved to a different net each time. The user therefore directed a revert and deferred the structural congestion work. After `git checkout` of both files the tree returned to `97c061c` and `scripts/build-gate1.sh` at route option 1 reproduced entry 45's numbers exactly, placement 1 at 70.203 MHz, placement 3 meeting timing at 75.660 MHz and placement 4 at 56.018 MHz, and the placement 3 image hashes to `6dbe2e37a3220740ba9b8258eed3f5cd6f019ee08c009c37e4576be3c4c0c822`, byte-identical to the image already installed as `cores/console138k/tang-psx.bin`, so the shipped core rebuilds deterministically from the committed tree, the loss of closure was caused by the reverted changes rather than by build noise, and no deployment was needed. The three reverted changes are described in this entry and nowhere else. The core-syntax audit re-read `.ai/core.md` and `.ai/core-syntax.md`, confirmed that `.ai/core.md` is unchanged, inspected the complete `.ai` diff, validated this entry as number 46 with exactly six required sections, confirmed that 46 active entries remain below the 100-entry limit, and confirmed that no settled history was rewritten.

#### Next Steps:

No timing work is outstanding: the tree at `97c061c` holds the core that ships, it closes deterministically at placement 3 with a 0.9 percent `sys_clk` margin, and a rebuild byte-identically reproduces the deployed image, so no build or deployment is needed until the source changes. Route option 2, placement option 0, and the two-file RTL change of this cycle are recorded as failures not to be repeated, and structural congestion work stays deferred at the user's direction. The next actionable work is the GPU step 2a follow-on that entry 45 queued, decoupling the stepper so outside-triangle steps overlap other work, adding the texture and CLUT caches for the read waits, deepening or batching the descriptor feed for the 48 words per primitive, or moving the 9.3 ms display copy to hardware scanout; whichever the user picks should be validated with `tests/test_psx_gpu_accel.py` before a closing build, and measured on hardware, since the replay cannot model arbiter contention.

#### Files Modified:

None.

#### Status:

- Build: PASS
- Deployment: N/A
- User Test: N/A

---
