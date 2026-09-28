#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-only

import argparse
import os
from pathlib import Path

from migen import Cat, Case, ClockDomain, ClockSignal, If, Instance, Mux, ResetSignal, Signal
from migen.genlib.cdc import MultiReg
from migen.genlib.resetsync import AsyncResetSynchronizer

from litex.gen import LiteXModule
from litex.build.generic_platform import IOStandard
from litex.soc.cores.clock.gowin_gw5a import GW5APLL
from litex.soc.integration.builder import Builder
from litex.soc.integration.soc import SoCRegion
from litex.soc.interconnect import ahb as litex_ahb
from litex.soc.interconnect.csr import AutoCSR, CSRStorage
from litex.soc.cores.cpu.gowin_ae350.core import GowinAE350

from litex_boards.targets import sipeed_tang_console as tang_console

import gowin_ddr3


ROOT = Path(__file__).resolve().parents[1]
PHOSPHOR = ROOT / "third_party" / "tang-phosphor"
MAIN_RAM_BASE = 0x4000_0000


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
    """Board clock, 75 MHz system PLL, and the AE350's dedicated PLL.

    The DDR3 controller owns its 400 MHz PLL and 100 MHz user clock
    (gowin_ddr3.GowinDDR3); the system domain reaches it through a native-port
    clock-domain crossing.
    """

    def __init__(self, platform, sys_clk_freq, with_sdram=False,
            sdram_rate="1:1", with_ddr3=False, ddr3_rate="1:2",
            with_video_pll=False, with_pcie=False, without_pll=False):
        assert not with_ddr3 and not with_sdram and not with_video_pll and not with_pcie
        assert not without_pll

        self.rst = Signal()
        self.cd_por = ClockDomain()
        self.cd_sys = ClockDomain()
        self.cd_cpu = ClockDomain(reset_less=True)
        self.cd_diag = ClockDomain()
        self.cpu_pll_lock = cpu_pll_lock = Signal()
        self.ddr_rst = Signal()

        self.clk50 = clk50 = platform.request("clk50")
        # Console EX_KEY.0 is active-low. The upstream target currently treats
        # this resource as active-high, which permanently reset its PLL.
        self.external_reset_n = reset_n = platform.request("rst")

        por_count = Signal(16, reset=2**16 - 1)
        por_done = Signal()
        self.comb += [
            self.cd_por.clk.eq(clk50),
            por_done.eq(por_count == 0),
            self.ddr_rst.eq(~por_done | ~reset_n),
        ]
        self.sync.por += If(~por_done, por_count.eq(por_count - 1))

        self.pll = pll = GW5APLL(
            devicename=platform.devicename, device=platform.device)
        pll.vco_freq_range = (650e6, 1300e6)
        self.comb += pll.reset.eq(~por_done | ~reset_n | self.rst)
        pll.register_clkin(clk50, 50e6)
        pll.create_clkout(self.cd_sys, sys_clk_freq)
        # Constrain the PLL output net, which synthesis keeps; the sys_clk alias
        # is merged away.
        config = pll.compute_config()
        platform.add_generated_clock_constraint(pll.clkouts[0].clk, clk50,
            multiply_by=config["fdiv"] * config["mdiv"],
            divide_by=config["idiv"] * config["odiv0"],
            name="sys_clk")

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
        self._fail_address = CSRStorage(32, reset=0, description="Address of the first failed check")
        self._fail_expected = CSRStorage(32, reset=0, description="Expected value of the first failed check")
        self._fail_observed = CSRStorage(32, reset=0, description="Observed value of the first failed check")
        self._log = []
        for index in range(32):
            register = CSRStorage(32, name=f"log{index}",
                description=f"Firmware log ring word {index}")
            setattr(self, f"_log{index}", register)
            self._log.append(register)


class Gate1SoC(tang_console.BaseSoC):
    def __init__(self, ip_dir, place_option=3):
        # The AE350 RAM port is bridged straight to the DDR3 native port,
        # bypassing the SoC interconnect.
        GowinAE350.native_memory = True
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
                with_ddr3=False,
            )
        finally:
            litex_ahb.AHB2Wishbone = upstream_ahb2wishbone

        # DDR3: Gowin controller behind the AE350's direct native memory bus.
        self.platform.add_extension(gowin_ddr3.ddram32_io())
        self.ddr3 = gowin_ddr3.GowinDDR3(self.platform,
            pads   = self.platform.request("ddram32"),
            clk50  = self.crg.clk50,
            rst    = self.crg.ddr_rst,
            ip_dir = ip_dir,
        )
        self.bus.add_region("main_ram", SoCRegion(
            origin=MAIN_RAM_BASE, size=gowin_ddr3.SIZE, mode="rwx"))
        self.cpu.add_memory_buses(address_width=32, data_width=gowin_ddr3.DATA_WIDTH)
        cpu_port = self.cpu.memory_buses[0]
        ddr_port = self.ddr3.port
        self.comb += [
            cpu_port.cmd.connect(ddr_port.cmd, omit={"addr"}),
            ddr_port.cmd.addr.eq(cpu_port.cmd.addr[:gowin_ddr3.ADDRESS_BITS]),
            cpu_port.wdata.connect(ddr_port.wdata),
            ddr_port.rdata.connect(cpu_port.rdata),
        ]
        # Clock groups are exclusive, as in the controller's reference design;
        # every crossing between them is synchronized.
        self.platform.add_false_path_constraints(
            self.crg.cd_sys.clk, self.crg.cd_diag.clk, self.crg.clk50,
            self.ddr3.cd_ddr.clk, self.ddr3.memory_clk)

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
        ddr_status = Signal(3)
        calib_time = Signal(32)
        calib_seen = Signal()

        # Controller state and calibration time in the diagnostic domain, which
        # keeps running while the DDR clocks start.
        self.specials += MultiReg(self.ddr3.status_sys, ddr_status, "diag")
        self.sync.diag += If(~calib_seen,
            calib_time.eq(calib_time + 1),
            calib_seen.eq(ddr_status[0]),
        )

        # Cat() preserves the intended one-bit widths.  Arithmetic shifts of
        # Migen's bitwise complement sign-extended the unused upper bits.
        self.comb += clock_status.eq(Cat(
            self.crg.cpu_pll_lock,
            ResetSignal("sys"),
            ddr_status[1],
            ddr_status[0],
            self.crg.pll.locked,
            self.cpu.reset,
            ~self.crg.external_reset_n,
            ddr_status[2],
        ))

        debug_registers = {
            0x00: debug_rdata_comb.eq(0x54505831),
            0x04: debug_rdata_comb.eq(0x00020000),
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
            0x34: debug_rdata_comb.eq(calib_time),
            0xc0: debug_rdata_comb.eq(self.gate1._fail_address.storage),
            0xc4: debug_rdata_comb.eq(self.gate1._fail_expected.storage),
            0xc8: debug_rdata_comb.eq(self.gate1._fail_observed.storage),
        }
        for index, register in enumerate(self.gate1._log):
            debug_registers[0x40 + 4*index] = debug_rdata_comb.eq(register.storage)

        self.comb += debug_rdata_comb.eq(0)
        # All directly decoded Gate 1 registers are aligned below 0x100. Decode
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


    def add_csr_bridge(self, name="csr", origin=None, with_register=False):
        # LiteX only registers the CSR bridge when a LiteDRAM core is present.
        # Keep the registered bridge the LiteDRAM-based Gate 1 used, so the
        # CSR decode is not in the same cycle as the AE350 peripheral bus.
        super().add_csr_bridge(name=name, origin=origin, with_register=True)


def main():
    parser = argparse.ArgumentParser(description="Build Tang-PSX Gate 1")
    parser.add_argument("--build", action="store_true", help="Run Gowin synthesis and place-and-route")
    parser.add_argument("--output-dir", default=str(ROOT / "build/gate1"))
    parser.add_argument("--ip-dir", default=str(ROOT / "build/gate1-ip"),
        help="Gowin DDR3/PLL IP generated by scripts/gen-ddr3-ip.sh")
    parser.add_argument("--place-option", type=int, default=3,
        help="Gowin placement strategy (default: 3)")
    args = parser.parse_args()

    soc = Gate1SoC(ip_dir=args.ip_dir, place_option=args.place_option)
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
