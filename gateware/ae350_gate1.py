#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-only

import argparse
import os
from pathlib import Path

from migen import Cat, Case, ClockDomain, ClockSignal, If, Instance, Mux, ResetSignal, Signal, log2_int
from migen.genlib.cdc import MultiReg, PulseSynchronizer
from migen.genlib.resetsync import AsyncResetSynchronizer

from litex.gen import LiteXModule
from litex.build.generic_platform import IOStandard
from litex.soc.cores.clock.gowin_gw5a import GW5APLL
from litex.soc.integration.builder import Builder
from litex.soc.integration.soc import SoCRegion
from litex.soc.interconnect import ahb as litex_ahb
from litex.soc.interconnect.csr import AutoCSR, CSRStorage
from litex.soc.cores.cpu.gowin_ae350.core import GowinAE350
from litex.soc.cores.video import VideoGowinHDMIPHY

from litex_boards.targets import sipeed_tang_console as tang_console

from litedram.common import LiteDRAMNativePort

import gowin_ddr3
from ae350_ram_bridge import Gate1RAMBridge
from ddr3_port_arbiter import DDR3PortArbiter
from stream_loader import StreamLoader


ROOT = Path(__file__).resolve().parents[1]
PHOSPHOR = ROOT / "third_party" / "tang-phosphor"

# AE350 PLL output dividers on its 750 MHz VCO: the CPU core clock (on
# CLKOUT1, the AE350's dedicated path) and an unused bus clock (on CLKOUT0).
# Overridable for clock-path diagnostics; see gateware/ae350_pll.v.
AE350_VCO_HZ   = 750e6
AE350_CPU_ODIV = 1
AE350_BUS_ODIV = 10
MAIN_RAM_BASE = 0x4000_0000
FRAMEBUFFER_BASE = 0x7ff0_0000
FRAMEBUFFER_FIFO_BYTES = 4096
HDMI_TIMINGS = {
    # The proven Tang Console PHY uses a 125 MHz serializer clock divided by
    # five.  Standard 640x480 blanking at the resulting 25 MHz pixel clock is
    # 59.52 Hz, close enough for normal HDMI/DVI sinks while keeping the video
    # and 75 MHz system clocks on the existing PLL.
    "pix_clk":       25e6,
    "h_active":      640,
    "h_blanking":    160,
    "h_sync_offset": 16,
    "h_sync_width":  96,
    "v_active":      480,
    "v_blanking":    45,
    "v_sync_offset": 10,
    "v_sync_width":  2,
}


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
    """Board clock, system/video PLL, and the AE350's dedicated PLL.

    The DDR3 controller owns its 400 MHz PLL and 100 MHz user clock
    (gowin_ddr3.GowinDDR3); the system domain reaches it through a native-port
    clock-domain crossing.  The system PLL also produces the 125 MHz HDMI
    serializer clock, divided by five for the pixel domain.
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
        self.cd_hdmi = ClockDomain()
        self.cd_hdmi5x = ClockDomain()
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
        pll.create_clkout(self.cd_hdmi5x, 125e6, margin=1e-3)
        self.specials += Instance("CLKDIV",
            p_DIV_MODE = "5",
            i_HCLKIN   = self.cd_hdmi5x.clk,
            i_RESETN   = 1,
            i_CALIB    = 0,
            o_CLKOUT   = self.cd_hdmi.clk,
        )
        # Constrain the PLL output net, which synthesis keeps; the sys_clk alias
        # is merged away.
        config = pll.compute_config()
        platform.add_generated_clock_constraint(pll.clkouts[0].clk, clk50,
            multiply_by=config["fdiv"] * config["mdiv"],
            divide_by=config["idiv"] * config["odiv0"],
            name="sys_clk")
        platform.add_generated_clock_constraint(pll.clkouts[1].clk, clk50,
            multiply_by=config["fdiv"] * config["mdiv"],
            divide_by=config["idiv"] * config["odiv1"],
            name="hdmi5x_clk")
        platform.add_generated_clock_constraint(self.cd_hdmi.clk,
            pll.clkouts[1].clk, divide_by=5, name="hdmi_clk")

        platform.add_source(str(ROOT / "gateware/ae350_pll.v"))
        self.specials += Instance("ae350_pll",
            p_CPU_ODIV = AE350_CPU_ODIV,
            p_BUS_ODIV = AE350_BUS_ODIV,
            i_clkin   = ClockSignal("por"),
            o_lock    = cpu_pll_lock,
            o_cpu_clk = self.cd_cpu.clk,
            o_bus_clk = Signal(),
        )

        # The Tang-Control transport runs from the 50 MHz board clock. That keeps it
        # independent of the system and DDR3 clocks, and leaves timing margin for
        # iosys_bl616's live stream logic, which misses 75 MHz. It is constrained as clk50.
        self.comb += self.cd_diag.clk.eq(clk50)

        # The AE350 macro receives the system-domain reset directly. Holding the
        # CPU clock domain in reset is still useful for generated cross-domain
        # logic and documents the PLL-lock dependency.
        self.specials += [
            AsyncResetSynchronizer(self.cd_cpu,  ~cpu_pll_lock),
            AsyncResetSynchronizer(self.cd_diag, ~por_done),
            AsyncResetSynchronizer(self.cd_hdmi, ~pll.locked),
        ]
        platform.add_period_constraint(self.cd_cpu.clk,
            1e9 * AE350_CPU_ODIV / AE350_VCO_HZ)
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
        self._loader_state = CSRStorage(32, reset=0, description="Loader state (bits 7:0) and completed runs (bits 31:16)")
        self._loader_bytes = CSRStorage(32, reset=0, description="Payload bytes of the last image")
        self._loader_crc = CSRStorage(32, reset=0, description="Computed CRC-32 of the last image payload")
        self._loader_result = CSRStorage(32, reset=0, description="Return value of the last program")
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
        # The AE350 RAM port is split by Gate1RAMBridge, which registers the DDR3 Wishbone path
        # so no combinational path runs from the LiteDRAM frontend into the AE350 macro.
        region = self.bus.regions["main_ram"]
        cpu_port = LiteDRAMNativePort("both",
            address_width = 32 - log2_int(gowin_ddr3.DATA_WIDTH//8),
            data_width    = gowin_ddr3.DATA_WIDTH)
        self.cpu.ram_bridge = Gate1RAMBridge(
            self.cpu.ahb_ram, self.cpu.dbus, cpu_port, region.origin, region.size)
        self.cpu.memory_buses.append(cpu_port)
        video_port = LiteDRAMNativePort("read",
            address_width = 32 - log2_int(gowin_ddr3.DATA_WIDTH//8),
            data_width    = gowin_ddr3.DATA_WIDTH)
        shared_port = LiteDRAMNativePort("both",
            address_width = 32 - log2_int(gowin_ddr3.DATA_WIDTH//8),
            data_width    = gowin_ddr3.DATA_WIDTH)
        self.ddr3_arbiter = DDR3PortArbiter(cpu_port, video_port, shared_port)
        ddr_port = self.ddr3.port
        self.comb += [
            shared_port.cmd.connect(ddr_port.cmd, omit={"addr"}),
            ddr_port.cmd.addr.eq(shared_port.cmd.addr[:gowin_ddr3.ADDRESS_BITS]),
            shared_port.wdata.connect(ddr_port.wdata),
            ddr_port.rdata.connect(shared_port.rdata),
        ]
        # Clock groups are exclusive, as in the controller's reference design;
        # every crossing between them is synchronized.
        self.platform.add_false_path_constraints(
            self.crg.cd_sys.clk, self.crg.clk50,
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

        # DDR-backed visible milestone: RGB565 pixels share the vendor DDR3
        # port with the AE350, with enough FIFO storage for more than three
        # active scan lines. Firmware draws the initial diagnostic image and
        # enables the DMA only after its memory checks finish.
        hdmi = self.platform.request("hdmi")
        self.comb += [
            hdmi.hdp.eq(1),
            hdmi.pwr_sav.eq(0),
        ]
        self.videophy = VideoGowinHDMIPHY(hdmi, clock_domain="hdmi")
        self.add_video_framebuffer(
            phy=self.videophy,
            timings=("640x480@59.52Hz", HDMI_TIMINGS),
            clock_domain="hdmi",
            format="rgb565",
            fifo_depth=FRAMEBUFFER_FIFO_BYTES,
            base=FRAMEBUFFER_BASE,
            dma_port=video_port,
        )
        # The framebuffer region already exports VIDEO_FRAMEBUFFER_BASE in
        # generated/mem.h. Avoid exporting the identical SoC constant too,
        # which warns when LiteX software includes mem.h before soc.h.
        del self.constants["VIDEO_FRAMEBUFFER_BASE"]

        video_underflow_seen = Signal()
        video_underflow_diag = Signal()
        video_enable_diag = Signal()
        self.sync.hdmi += If(self.video_framebuffer.underflow,
            video_underflow_seen.eq(1))
        self.specials += [
            MultiReg(video_underflow_seen, video_underflow_diag, "diag"),
            MultiReg(self.video_framebuffer.dma._enable.storage, video_enable_diag, "diag"),
        ]
        video_status = Signal(32)
        self.comb += video_status.eq(Cat(video_enable_diag, video_underflow_diag))

        # Do not release the hard core before its dedicated PLL locks.  The
        # generic wrapper only includes the system-domain reset by default.
        # Tang-Control can restart the AE350 by writing 1 to debug address 0x100, so a hung
        # program can be replaced without reloading the FPGA.
        loader_cpu_reset = Signal()
        self.cpu.cpu_params["i_HW_RSTN"] = ~(
            ResetSignal("sys") | self.cpu.reset | ~self.crg.cpu_pll_lock | loader_cpu_reset)

        self.gate1 = Gate1Status()
        self.loader = loader = StreamLoader(stream_domain="diag")

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

        reset_request = Signal()
        reset_count = Signal(5)
        self.reset_pulse = reset_pulse = PulseSynchronizer("diag", "sys")
        self.comb += [
            reset_request.eq(debug_valid & debug_write & (debug_address == 0x100) & debug_wdata[0]),
            reset_pulse.i.eq(reset_request),
            loader_cpu_reset.eq(reset_count != 0),
        ]
        self.sync += If(reset_pulse.o,
            reset_count.eq(31),
        ).Elif(reset_count != 0,
            reset_count.eq(reset_count - 1),
        )

        debug_registers = {
            0x00: debug_rdata_comb.eq(0x54505831),
            0x04: debug_rdata_comb.eq(0x00020001),
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
            0xd0: debug_rdata_comb.eq(self.gate1._loader_state.storage),
            0xd4: debug_rdata_comb.eq(self.gate1._loader_bytes.storage),
            0xd8: debug_rdata_comb.eq(self.gate1._loader_crc.storage),
            0xdc: debug_rdata_comb.eq(self.gate1._loader_result.storage),
            0xe0: debug_rdata_comb.eq(loader.sessions),
            0xe4: debug_rdata_comb.eq(loader.bytes),
            0xe8: debug_rdata_comb.eq(loader.ends),
            0xec: debug_rdata_comb.eq(loader.cancels),
            0xf0: debug_rdata_comb.eq(loader.overflow),
            0xf4: debug_rdata_comb.eq(video_status),
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
            p_FREQ=50_000_000,
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
            o_stream_start=loader.start,
            o_stream_end=loader.end,
            o_stream_cancel=loader.cancel,
            o_stream_data=loader.data,
            o_stream_valid=loader.valid,
            i_stream_ready=loader.ready,
            i_uart_rx=serial.rx,
            o_uart_tx=serial.tx,
        )


    def add_csr_bridge(self, name="csr", origin=None, with_register=False):
        # LiteX only registers the CSR bridge when a LiteDRAM core is present.
        # Keep the registered bridge the LiteDRAM-based Gate 1 used, so the
        # CSR decode is not in the same cycle as the AE350 peripheral bus.
        super().add_csr_bridge(name=name, origin=origin, with_register=True)


def main():
    global AE350_CPU_ODIV, AE350_BUS_ODIV
    parser = argparse.ArgumentParser(description="Build Tang-PSX Gate 1")
    parser.add_argument("--build", action="store_true", help="Run Gowin synthesis and place-and-route")
    parser.add_argument("--output-dir", default=str(ROOT / "build/gate1"))
    parser.add_argument("--ip-dir", default=str(ROOT / "build/gate1-ip"),
        help="Gowin DDR3/PLL IP generated by scripts/gen-ddr3-ip.sh")
    parser.add_argument("--place-option", type=int, default=3,
        help="Gowin placement strategy (default: 3)")
    parser.add_argument("--ae350-cpu-odiv", type=int, default=AE350_CPU_ODIV,
        help="AE350 PLL CPU-clock (CLKOUT1) divider on the 750 MHz VCO")
    parser.add_argument("--ae350-bus-odiv", type=int, default=AE350_BUS_ODIV,
        help="AE350 PLL bus-clock (CLKOUT0, unused) divider")
    args = parser.parse_args()

    AE350_CPU_ODIV = args.ae350_cpu_odiv
    AE350_BUS_ODIV = args.ae350_bus_odiv

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
