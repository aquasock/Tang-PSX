#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-only

import argparse
import os
from pathlib import Path

from migen import Cat, Case, ClockDomain, ClockSignal, If, Instance, Mux, Replicate, ResetSignal, Signal
from migen.genlib.cdc import MultiReg
from migen.genlib.resetsync import AsyncResetSynchronizer

from litex.gen import LiteXModule
from litex.build.generic_platform import IOStandard
from litex.soc.cores.clock.gowin_gw5a import GW5APLL
from litex.soc.integration.builder import Builder
from litex.soc.interconnect import ahb as litex_ahb
from litex.soc.interconnect.csr import AutoCSR, CSR, CSRField, CSRStatus, CSRStorage
from litex.soc.cores.cpu.gowin_ae350.core import GowinAE350

import litedram.dfii as litedram_dfii
from litex_boards.targets import sipeed_tang_console as tang_console


ROOT = Path(__file__).resolve().parents[1]
PHOSPHOR = ROOT / "third_party" / "tang-phosphor"


class Gate1PhaseInjector(LiteXModule, AutoCSR):
    """LiteDRAM software DFI phase with a registered command issue path.

    The upstream PhaseInjector drives the physical DFI command directly from
    the CSR address decoder. At 75 MHz that creates a real CPU-CSR-to-OSER
    path. Capturing the already-programmed command payload when the issue CSR
    is written adds one system cycle to software commands and leaves the
    hardware-controller DFI path untouched.
    """

    def __init__(self, phase):
        self._command = CSRStorage(fields=[
            CSRField("cs",        size=1, description="DFI chip select bus"),
            CSRField("we",        size=1, description="DFI write enable bus"),
            CSRField("cas",       size=1, description="DFI column address strobe bus"),
            CSRField("ras",       size=1, description="DFI row address strobe bus"),
            CSRField("wren",      size=1, description="DFI write data enable bus"),
            CSRField("rden",      size=1, description="DFI read data enable bus"),
            CSRField("cs_top",    size=1, description="Top clam-shell chip select"),
            CSRField("cs_bottom", size=1, description="Bottom clam-shell chip select"),
        ], description="Control DFI signals on a single phase")
        self._command_issue = CSR()
        self._address = CSRStorage(len(phase.address), reset_less=True,
            description="DFI address bus")
        self._baddress = CSRStorage(len(phase.bank), reset_less=True,
            description="DFI bank address bus")
        self._wrdata = CSRStorage(len(phase.wrdata), reset_less=True,
            description="DFI write data bus")
        self._rddata = CSRStatus(len(phase.rddata), description="DFI read data bus")

        issue = Signal()
        command = Signal(8)
        address = Signal(len(phase.address), reset_less=True)
        baddress = Signal(len(phase.bank), reset_less=True)
        wrdata = Signal(len(phase.wrdata), reset_less=True)

        self.sync += [
            issue.eq(self._command_issue.wr_stb),
            If(self._command_issue.wr_stb,
                command.eq(self._command.storage),
                address.eq(self._address.storage),
                baddress.eq(self._baddress.storage),
            ),
            If(self._wrdata.wr_stb,
                wrdata.eq(self._wrdata.storage),
            ),
            If(phase.rddata_valid, self._rddata.status.eq(phase.rddata)),
        ]
        self.comb += [
            If(issue,
                If(command[6],
                    phase.cs_n.eq(2),
                ).Elif(command[7],
                    phase.cs_n.eq(1),
                ).Else(
                    phase.cs_n.eq(Replicate(~command[0], len(phase.cs_n))),
                ),
                phase.we_n.eq(~command[1]),
                phase.cas_n.eq(~command[2]),
                phase.ras_n.eq(~command[3]),
            ).Else(
                phase.cs_n.eq(Replicate(1, len(phase.cs_n))),
                phase.we_n.eq(1),
                phase.cas_n.eq(1),
                phase.ras_n.eq(1),
            ),
            phase.address.eq(address),
            phase.bank.eq(baddress),
            phase.wrdata_en.eq(issue & command[4]),
            phase.rddata_en.eq(issue & command[5]),
            phase.wrdata.eq(wrdata),
            phase.wrdata_mask.eq(0),
        ]


# DFIInjector resolves this module global when LiteDRAMCore is constructed.
litedram_dfii.PhaseInjector = Gate1PhaseInjector


class Gate1PeripheralAHB2Wishbone(LiteXModule):
    """AE350 peripheral bridge with explicit always-enabled payload flops.

    Gowin synthesis maps conditional assignments to the Wishbone payload
    registers onto their clock-enable pins. On the crowded 75 MHz Gate 1
    image, the upstream bridge's FSM-to-CE route misses timing even though the
    data inputs have ample margin. Keep the same registered AHB response and
    two-cycle Wishbone transaction, but implement the held address/control
    payload with DFFRE primitives whose enables are tied high. The transaction
    state then selects a local D-input hold mux instead of driving a distributed
    clock-enable net.

    This bridge is deliberately limited to the AE350's 32-bit peripheral port.
    ROM and RAM retain LiteX's normal bridges.
    """

    IDLE  = 0
    DATA  = 1
    ERROR = 2

    def __init__(self, ahb, wishbone, with_bursting=False):
        assert not with_bursting
        assert ahb.data_width == 32
        assert wishbone.data_width == 32
        assert wishbone.addressing == "word"
        assert ahb.address_width == wishbone.adr_width + 2

        state = Signal(2, reset=self.IDLE)
        error_second = Signal()
        capture = Signal()
        decoded_sel = Signal(4)
        valid = Signal()
        supported = Signal()

        self.comb += [
            valid.eq(ahb.sel & ahb.trans[1]),
            supported.eq(ahb.size <= 2),
            decoded_sel.eq(0),
            Case(ahb.size, {
                0: Case(ahb.addr[0:2], {
                    0: decoded_sel.eq(0b0001),
                    1: decoded_sel.eq(0b0010),
                    2: decoded_sel.eq(0b0100),
                    3: decoded_sel.eq(0b1000),
                }),
                1: Case(ahb.addr[1:2], {
                    0: decoded_sel.eq(0b0011),
                    1: decoded_sel.eq(0b1100),
                }),
                2: decoded_sel.eq(0b1111),
            }),
            ahb.readyout.eq(0),
            ahb.resp.eq(0),
            wishbone.stb.eq(0),
            wishbone.cyc.eq(0),
            wishbone.dat_w.eq(ahb.wdata),
            capture.eq(0),
            Case(state, {
                self.IDLE: [
                    ahb.readyout.eq(1),
                    ahb.resp.eq(error_second),
                    capture.eq(valid & supported),
                ],
                self.DATA: [
                    wishbone.stb.eq(1),
                    wishbone.cyc.eq(1),
                ],
                self.ERROR: ahb.resp.eq(1),
            }),
        ]

        self.sync += Case(state, {
            self.IDLE: [
                error_second.eq(0),
                If(valid,
                    If(supported,
                        state.eq(self.DATA),
                    ).Else(
                        state.eq(self.ERROR),
                    ),
                ),
            ],
            self.DATA: [
                If(wishbone.err,
                    state.eq(self.ERROR),
                ).Elif(wishbone.ack,
                    ahb.rdata.eq(wishbone.dat_r),
                    state.eq(self.IDLE),
                ),
            ],
            self.ERROR: [
                error_second.eq(1),
                state.eq(self.IDLE),
            ],
        })

        def add_payload_flops(target, source):
            for bit in range(len(target)):
                self.specials += Instance("DFFRE",
                    p_INIT  = 0,
                    i_D     = Mux(capture, source[bit], target[bit]),
                    i_CLK   = ClockSignal("sys"),
                    i_CE    = 1,
                    i_RESET = ResetSignal("sys"),
                    o_Q     = target[bit],
                )

        add_payload_flops(wishbone.adr, ahb.addr[2:])
        add_payload_flops(wishbone.we, ahb.write)
        add_payload_flops(wishbone.sel, decoded_sel)


class Gate1CRG(LiteXModule):
    """Console 138K DDR clocks plus the AE350's dedicated PLL."""

    def __init__(self, platform, sys_clk_freq, with_sdram=False,
            sdram_rate="1:1", with_ddr3=False, ddr3_rate="1:2",
            with_video_pll=False, with_pcie=False, without_pll=False):
        assert with_ddr3 and ddr3_rate in ("1:2", "1:4")
        assert not with_sdram and not with_video_pll and not with_pcie
        assert not without_pll

        ddr3_nphases = int(ddr3_rate[-1])

        self.rst = Signal()
        self.stop = Signal()
        self.reset = Signal()
        self.cd_por = ClockDomain()
        self.cd_init = ClockDomain()
        self.cd_sys = ClockDomain()
        cd_ddr = ClockDomain(f"sys{ddr3_nphases}x")
        cd_ddr_i = ClockDomain(f"sys{ddr3_nphases}x_i")
        setattr(self, f"cd_sys{ddr3_nphases}x", cd_ddr)
        setattr(self, f"cd_sys{ddr3_nphases}x_i", cd_ddr_i)
        self.cd_cpu = ClockDomain(reset_less=True)
        self.cd_diag = ClockDomain()
        self.cpu_pll_lock = cpu_pll_lock = Signal()

        clk50 = platform.request("clk50")
        # Console EX_KEY.0 is active-low. The upstream target currently treats
        # this resource as active-high, which permanently reset its PLL.
        self.external_reset_n = reset_n = platform.request("rst")

        por_count = Signal(16, reset=2**16 - 1)
        por_done = Signal()
        self.comb += [
            self.cd_por.clk.eq(clk50),
            por_done.eq(por_count == 0),
        ]
        self.sync.por += If(~por_done, por_count.eq(por_count - 1))

        self.pll = pll = GW5APLL(
            devicename=platform.devicename, device=platform.device)
        pll.vco_freq_range = (650e6, 1300e6)
        self.comb += pll.reset.eq(~por_done | ~reset_n | self.rst)
        pll.register_clkin(clk50, 50e6)
        pll.create_clkout(cd_ddr_i, ddr3_nphases * sys_clk_freq)

        self.specials += [
            Instance("DHCE",
                i_CLKIN=cd_ddr_i.clk,
                i_CEN=self.stop,
                o_CLKOUT=cd_ddr.clk,
            ),
            Instance("CLKDIV",
                p_DIV_MODE=str(ddr3_nphases),
                i_CALIB=0,
                i_HCLKIN=cd_ddr.clk,
                i_RESETN=~self.reset,
                o_CLKOUT=self.cd_sys.clk,
            ),
            AsyncResetSynchronizer(self.cd_sys, ~pll.locked | self.reset),
        ]
        self.comb += [
            self.cd_init.clk.eq(clk50),
            self.cd_init.rst.eq(pll.reset),
        ]

        config = pll.compute_config()
        ddr_clk = pll.clkouts[0].clk
        platform.add_generated_clock_constraint(ddr_clk, clk50,
            multiply_by=config["fdiv"] * config["mdiv"],
            divide_by=config["idiv"] * config["odiv0"])
        platform.add_generated_clock_constraint(self.cd_sys.clk,
            ddr_clk, divide_by=ddr3_nphases)

        platform.add_source(str(PHOSPHOR / "src/ae350/ae350_pll.v"))
        self.specials += Instance("ae350_pll",
            i_clkin   = ClockSignal("por"),
            o_lock    = cpu_pll_lock,
            o_cpu_clk = self.cd_cpu.clk,
            o_bus_clk = self.cd_diag.clk,
        )

        # The AE350 macro receives the system-domain reset directly. Holding the
        # CPU clock domain in reset is still useful for generated cross-domain
        # logic and documents the PLL-lock dependency.
        self.specials += [
            AsyncResetSynchronizer(self.cd_cpu,  ~cpu_pll_lock),
            AsyncResetSynchronizer(self.cd_diag, ~cpu_pll_lock),
        ]
        platform.add_period_constraint(self.cd_cpu.clk, 1e9 / 750e6)
        platform.add_period_constraint(self.cd_diag.clk, 1e9 / 75e6)
        platform.toolchain.additional_cst_commands.append(
            'INS_LOC "ae350_pll/PLL_inst" PLL_R[0];')


# BaseSoC resolves this module-level symbol when it creates its CRG.
tang_console._CRG = Gate1CRG


class Gate1Status(LiteXModule, AutoCSR):
    def __init__(self):
        self._stage = CSRStorage(32, reset=0, description="Gate 1 firmware stage")
        self._failure = CSRStorage(32, reset=0, description="Gate 1 failure code")
        self._ddr_words = CSRStorage(32, reset=0, description="Verified DDR words")
        self._ddr_checksum = CSRStorage(32, reset=0, description="DDR checksum")
        self._jit_result = CSRStorage(32, reset=0, description="JIT probe results")
        self._cycles = CSRStorage(32, reset=0, description="Probe cycle count")
        self._features = CSRStorage(32, reset=0, description="Passed feature bitmap")
        self._log_head = CSRStorage(32, reset=0, description="Firmware log byte count")
        self._phy_burst_masks = CSRStorage(32, reset=0,
            description="Read-burst bitslip masks for lanes 0 and 1")
        self._phy_burst_counts = CSRStorage(32, reset=0,
            description="Read-burst hit counts for lanes 0 and 1")
        self._phy_first0 = CSRStorage(32, reset=0xffffffff,
            description="First lane-0 burst scan point")
        self._phy_first1 = CSRStorage(32, reset=0xffffffff,
            description="First lane-1 burst scan point")
        self._phy_raw0 = CSRStorage(32, reset=0, description="Captured DFI phase-0 read data")
        self._phy_raw1 = CSRStorage(32, reset=0, description="Captured DFI phase-1 read data")
        self._phy_raw2 = CSRStorage(32, reset=0, description="Captured DFI phase-2 read data")
        self._phy_raw3 = CSRStorage(32, reset=0, description="Captured DFI phase-3 read data")
        self._log = []
        for index in range(32):
            register = CSRStorage(32, name=f"log{index}",
                description=f"Firmware log ring word {index}")
            setattr(self, f"_log{index}", register)
            self._log.append(register)


class Gate1SoC(tang_console.BaseSoC):
    def __init__(self, place_option=3):
        # Keep the conservative fabric Wishbone path while establishing the
        # final 75 MHz PHY configuration. The direct path is revisited only
        # after this baseline passes deterministically.
        GowinAE350.native_memory = False
        # Select the timing-safe bridge only for the second non-bursting
        # 32-bit AHB bridge created by GowinAE350.__init__: its peripheral
        # port. Restore the module entry immediately so the deferred RAM
        # bridge and every other LiteX user retain the upstream implementation.
        upstream_ahb2wishbone = litex_ahb.AHB2Wishbone
        bridge_index = 0

        def gate1_ahb2wishbone(ahb, wishbone, with_bursting=False):
            nonlocal bridge_index
            if not with_bursting and ahb.data_width == 32 and wishbone.data_width == 32:
                bridge_index += 1
                if bridge_index == 2:
                    return Gate1PeripheralAHB2Wishbone(ahb, wishbone)
            return upstream_ahb2wishbone(ahb, wishbone, with_bursting=with_bursting)

        litex_ahb.AHB2Wishbone = gate1_ahb2wishbone
        try:
            super().__init__(
                device="GW5AST-138C",
                sys_clk_freq=75e6,
                cpu_type="gowin_ae350",
                cpu_variant="standard",
                integrated_rom_size=0x10000,
                integrated_sram_size=0x8000,
                with_uart=False,
                with_timer=True,
                with_led_chaser=False,
                with_buttons=False,
                with_ddr3=True,
                ddr3_rate="1:4",
            )
        finally:
            litex_ahb.AHB2Wishbone = upstream_ahb2wishbone

        # Gate 1 performs its own deterministic DDR test and reports the first
        # failing word/value through Tang-Control.  LiteDRAM's built-in memtest
        # only returns pass/fail and would hide that diagnostic information.
        self.add_constant("SDRAM_TEST_DISABLE")

        # The shared board file describes this pin as 1.5 V even though its own
        # note says the 138K routing is 3.3 V.  AE350 makes the bank voltage
        # conflict visible, so correct the already-requested resource locally.
        for index, (resource, signal) in enumerate(self.platform.constraint_manager.matched):
            if resource[0] == "fan_en":
                constraints = tuple(
                    IOStandard("LVCMOS33") if isinstance(item, IOStandard) else item
                    for item in resource[2:]
                )
                corrected = (resource[0], resource[1], *constraints)
                self.platform.constraint_manager.matched[index] = (corrected, signal)

        # The board definition normally frees the hard-CPU pins. Gate 1 uses
        # the AE350 macro, so the setting must be reversed before project emit.
        self.platform.toolchain.options["use_cpu_as_gpio"] = 0
        self.platform.toolchain.options["multi_boot"] = 1
        self.platform.toolchain.options["place_option"] = place_option
        # Tang-Control's proven transport uses SystemVerilog sized casts and
        # block-local declarations despite its historical .v filenames.
        self.platform.toolchain.options["verilog_std"] = "sysv2017"

        # Do not release the hard core before its dedicated PLL locks.  The
        # generic wrapper only includes the system-domain reset by default.
        self.cpu.cpu_params["i_HW_RSTN"] = ~(
            ResetSignal("sys") | self.cpu.reset | ~self.crg.cpu_pll_lock)

        self.gate1 = Gate1Status()

        serial = self.platform.request("serial")
        debug_valid = Signal()
        debug_write = Signal()
        debug_address = Signal(32)
        debug_wdata = Signal(32)
        debug_rdata = Signal(32)
        debug_rdata_comb = Signal(32)
        transport_crc_errors = Signal(32)
        transport_bad_requests = Signal(32)
        clock_status = Signal(32)
        training_read_steps = Signal(32)
        training_write_steps = Signal(32)
        training_commands = Signal(32)
        training_lane_diag = Signal(32)
        training_read_steps_diag = Signal(32)
        training_write_steps_diag = Signal(32)
        training_commands_diag = Signal(32)

        # Observe the software-driven PHY calibration without changing the
        # pinned LiteDRAM routine. These counters distinguish a long search
        # from a fixed CSR access and identify whether read or write taps move.
        read_step = (
            self.ddrphy._rdly_dq_rst.wr_stb |
            self.ddrphy._rdly_dq_inc.wr_stb |
            self.ddrphy._rdly_dq_bitslip_rst.wr_stb |
            self.ddrphy._rdly_dq_bitslip.wr_stb)
        write_step = (
            self.ddrphy._wdly_dq_rst.wr_stb |
            self.ddrphy._wdly_dq_inc.wr_stb)
        command_issue = (
            self.sdram.dfii.pi0._command_issue.wr_stb |
            self.sdram.dfii.pi1._command_issue.wr_stb |
            self.sdram.dfii.pi2._command_issue.wr_stb |
            self.sdram.dfii.pi3._command_issue.wr_stb)
        self.sync.sys += [
            If(read_step, training_read_steps.eq(training_read_steps + 1)),
            If(write_step, training_write_steps.eq(training_write_steps + 1)),
            If(command_issue, training_commands.eq(training_commands + 1)),
        ]
        self.specials += [
            MultiReg(self.ddrphy._dly_sel.storage, training_lane_diag, "diag"),
            MultiReg(training_read_steps, training_read_steps_diag, "diag"),
            MultiReg(training_write_steps, training_write_steps_diag, "diag"),
            MultiReg(training_commands, training_commands_diag, "diag"),
        ]

        # Cat() preserves the intended one-bit widths.  Arithmetic shifts of
        # Migen's bitwise complement sign-extended the unused upper bits.
        self.comb += clock_status.eq(Cat(
            self.crg.cpu_pll_lock,
            ResetSignal("sys"),
            self.crg.stop,
            self.crg.reset,
            self.crg.pll.locked,
            self.cpu.reset,
            ~self.crg.external_reset_n,
        ))

        debug_registers = {
            0x00: debug_rdata_comb.eq(0x54505831),
            0x04: debug_rdata_comb.eq(0x00010009),
            0x08: debug_rdata_comb.eq(self.gate1._stage.storage),
            0x0c: debug_rdata_comb.eq(self.gate1._failure.storage),
            0x10: debug_rdata_comb.eq(self.gate1._ddr_words.storage),
            0x14: debug_rdata_comb.eq(self.gate1._ddr_checksum.storage),
            0x18: debug_rdata_comb.eq(self.gate1._jit_result.storage),
            0x1c: debug_rdata_comb.eq(self.gate1._cycles.storage),
            0x20: debug_rdata_comb.eq(self.gate1._features.storage),
            0x24: debug_rdata_comb.eq(transport_crc_errors),
            0x28: debug_rdata_comb.eq(transport_bad_requests),
            0x2c: debug_rdata_comb.eq(clock_status),
            0x30: debug_rdata_comb.eq(self.gate1._log_head.storage),
            0x34: debug_rdata_comb.eq(training_lane_diag),
            0x38: debug_rdata_comb.eq(training_read_steps_diag),
            0x3c: debug_rdata_comb.eq(training_write_steps_diag),
            0xc0: debug_rdata_comb.eq(training_commands_diag),
            0xc4: debug_rdata_comb.eq(self.gate1._phy_burst_masks.storage),
            0xc8: debug_rdata_comb.eq(self.gate1._phy_burst_counts.storage),
            0xcc: debug_rdata_comb.eq(self.gate1._phy_first0.storage),
            0xd0: debug_rdata_comb.eq(self.gate1._phy_first1.storage),
            0xd4: debug_rdata_comb.eq(self.gate1._phy_raw0.storage),
            0xd8: debug_rdata_comb.eq(self.gate1._phy_raw1.storage),
            0xdc: debug_rdata_comb.eq(self.gate1._phy_raw2.storage),
            0xe0: debug_rdata_comb.eq(self.gate1._phy_raw3.storage),
        }
        for index, register in enumerate(self.gate1._log):
            debug_registers[0x40 + 4*index] = debug_rdata_comb.eq(register.storage)

        self.comb += debug_rdata_comb.eq(0)
        # All directly decoded Gate 1 registers are aligned below 0xe4. Decode
        # only the six meaningful word-index bits instead of building 32-bit
        # equality comparators for every entry in the log ring.
        self.comb += Case(debug_address[2:8], {
            address >> 2: assignment
            for address, assignment in debug_registers.items()
        })
        # The extended-read address is complete six UART bytes before
        # iosys_bl616 samples the result.  Registering the mux removes the
        # 32-word log decoder from the transport's response-data path while
        # preserving the protocol timing by a wide margin.
        self.sync.diag += debug_rdata.eq(debug_rdata_comb)

        for source in [
            "src/iosys/iosys_bl616.v",
            "src/iosys/uart_fixed.v",
            "src/iosys/textdisp.v",
            "src/iosys/gowin_dpb_menu.v",
        ]:
            self.platform.add_source(str(PHOSPHOR / source))

        self.specials += Instance("iosys_bl616",
            p_CORE_ID=0x0051,
            p_FREQ=75_000_000,
            i_clk=ClockSignal("diag"),
            i_hclk=ClockSignal("diag"),
            i_resetn=~ResetSignal("diag"),
            i_overlay_x=0,
            i_overlay_y=0,
            i_joy1=0,
            i_joy2=0,
            i_mgmt_readdata=0,
            i_fdd_request=0,
            o_debug_valid=debug_valid,
            o_debug_write=debug_write,
            o_debug_address=debug_address,
            o_debug_wdata=debug_wdata,
            i_debug_rdata=debug_rdata,
            o_debug_crc_errors=transport_crc_errors,
            o_debug_bad_requests=transport_bad_requests,
            i_stream_ready=1,
            i_uart_rx=serial.rx,
            o_uart_tx=serial.tx,
        )


def main():
    parser = argparse.ArgumentParser(description="Build Tang-PSX Gate 1")
    parser.add_argument("--build", action="store_true", help="Run Gowin synthesis and place-and-route")
    parser.add_argument("--output-dir", default=str(ROOT / "build/gate1"))
    parser.add_argument("--place-option", type=int, default=3,
        help="Gowin placement strategy (default: 3)")
    args = parser.parse_args()

    soc = Gate1SoC(place_option=args.place_option)
    builder = Builder(
        soc,
        output_dir=args.output_dir,
        compile_software=True,
        compile_gateware=args.build,
        csr_csv=os.path.join(args.output_dir, "csr.csv"),
        bios_console="disable",
    )
    custom_bios = str(ROOT / "software/gate1")
    # Builder adds the default BIOS lazily in build().  Register ours first so
    # the stock interactive BIOS (and its unsupported AE350 IRQ helpers) never
    # enters this deliberately headless image.
    builder.add_software_package("bios", custom_bios)
    builder.build(run=args.build, build_name="tang_psx_gate1")


if __name__ == "__main__":
    main()
