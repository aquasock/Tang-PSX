# SPDX-License-Identifier: GPL-3.0-only

"""Scale PSX VRAM into the Gate 1 RGB565 HDMI framebuffer."""

from migen import Array, Case, Cat, FSM, If, Memory, Mux, Signal

from litex.gen import LiteXModule, NextState, NextValue
from litex.soc.interconnect import wishbone
from litedram.common import LiteDRAMNativePort


MAGIC = 0x44535031                 # "DSP1"
MAIN_RAM_BASE = 0x4000_0000
VRAM_BASE = 0x7fe0_0000
FRAMEBUFFER_BASE = 0x7ff0_0000
VRAM_NATIVE_BASE = (VRAM_BASE - MAIN_RAM_BASE) // 32
FRAMEBUFFER_NATIVE_BASE = (FRAMEBUFFER_BASE - MAIN_RAM_BASE) // 32
SOURCE_WORDS = 1024 // 16
OUTPUT_WIDTH = 640
OUTPUT_HEIGHT = 480
OUTPUT_WORDS = OUTPUT_WIDTH // 16


class DisplayBlitter(LiteXModule):
    """Nearest-neighbor BGR555-to-RGB565 framebuffer blitter.

    Register 0 reads as MAGIC. Register 1 reports ready, busy, and error; write
    bit 0 to start. Registers 2 and 3 hold packed source origin and size.
    Registers 4 and 5 report completed frames and busy cycles.
    """

    def __init__(self, address_width=25):
        self.bus = bus = wishbone.Interface(
            data_width=32, address_width=32, addressing="word")
        self.port = port = LiteDRAMNativePort(
            "both", address_width=address_width, data_width=256)

        origin = Signal(32)
        size = Signal(32, reset=(240 << 16) | 320)
        display_x = Signal(10)
        display_y = Signal(9)
        source_width = Signal(10)
        source_height = Signal(10)
        source_y = Signal(9)
        cached_y = Signal(9)
        cached_valid = Signal()
        output_y = Signal(9)
        source_word = Signal(6)
        unpack_lane = Signal(4)
        input_line = Signal(256)
        output_word = Signal(6)
        output_lane = Signal(4)
        source_x = Signal(10)
        x_accum = Signal(11)
        y_accum = Signal(10)
        completed = Signal(32)
        busy_cycles = Signal(32)
        error = Signal()
        start = Signal()
        busy = Signal()
        request = Signal()
        read_data = Signal(32)

        row = Memory(16, 1024)
        row_write = row.get_port(write_capable=True)
        row_read = row.get_port(has_re=True)
        self.specials += row, row_write, row_read

        input_pixels = Array(
            input_line[16*n:16*(n + 1)] for n in range(16))
        output_pixels = [Signal(16) for _ in range(16)]
        source_pixel = row_read.dat_r
        rgb565 = Signal(16)
        x_sum = Signal(11)
        y_sum = Signal(10)

        self.comb += [
            request.eq(bus.cyc & bus.stb),
            start.eq(request & ~bus.ack & bus.we &
                (bus.adr[0:3] == 1) & bus.dat_w[0]),
            bus.err.eq(0),
            read_data.eq(0),
            Case(bus.adr[0:3], {
                0: read_data.eq(MAGIC),
                1: read_data.eq(Cat(~busy, busy, error)),
                2: read_data.eq(origin),
                3: read_data.eq(size),
                4: read_data.eq(completed),
                5: read_data.eq(busy_cycles),
            }),
            port.cmd.we.eq(0),
            port.cmd.addr.eq(0),
            port.cmd.first.eq(1),
            port.cmd.last.eq(1),
            port.wdata.valid.eq(0),
            port.wdata.data.eq(Cat(*output_pixels)),
            port.wdata.we.eq(2**32 - 1),
            port.wdata.first.eq(1),
            port.wdata.last.eq(1),
            port.rdata.ready.eq(0),
            port.flush.eq(0),
            row_write.we.eq(0),
            row_write.adr.eq((source_word << 4) | unpack_lane),
            row_write.dat_w.eq(input_pixels[unpack_lane]),
            row_read.re.eq(0),
            row_read.adr.eq(source_x),
            rgb565.eq(((source_pixel & 0x001f) << 11) |
                ((source_pixel & 0x03e0) << 1) |
                ((source_pixel & 0x7c00) >> 10)),
            x_sum.eq(x_accum + source_width),
            y_sum.eq(y_accum + source_height),
        ]

        self.sync += [
            bus.ack.eq(0),
            bus.dat_r.eq(read_data),
            If(request & ~bus.ack,
                bus.ack.eq(1),
                If(bus.we & (bus.adr[0:3] == 2),
                    origin.eq(bus.dat_w),
                ).Elif(bus.we & (bus.adr[0:3] == 3),
                    size.eq(bus.dat_w),
                ),
            ),
            If(busy, busy_cycles.eq(busy_cycles + 1)),
            If(start & busy, error.eq(1)),
        ]

        self.fsm = fsm = FSM(reset_state="IDLE")
        fsm.act("IDLE",
            If(start,
                If((size[:10] == 0) | (size[:10] > 640) |
                   (size[16:26] == 0) | (size[16:26] > 480),
                    NextValue(error, 1),
                ).Else(
                    NextValue(error, 0),
                    NextValue(busy, 1),
                    NextValue(busy_cycles, 0),
                    NextValue(display_x, origin[:10]),
                    NextValue(display_y, origin[16:25]),
                    NextValue(source_width, size[:10]),
                    NextValue(source_height, size[16:26]),
                    NextValue(source_y, origin[16:25]),
                    NextValue(cached_valid, 0),
                    NextValue(output_y, 0),
                    NextValue(y_accum, 0),
                    NextState("ROW-CHECK"),
                ),
            ),
        )
        fsm.act("ROW-CHECK",
            If(cached_valid & (cached_y == source_y),
                NextState("OUTPUT-START"),
            ).Else(
                NextValue(source_word, 0),
                NextState("READ-COMMAND"),
            ),
        )
        fsm.act("READ-COMMAND",
            port.cmd.valid.eq(1),
            port.cmd.addr.eq(VRAM_NATIVE_BASE + (source_y << 6) + source_word),
            If(port.cmd.ready,
                NextState("READ-DATA"),
            ),
        )
        fsm.act("READ-DATA",
            port.rdata.ready.eq(1),
            If(port.rdata.valid,
                NextValue(input_line, port.rdata.data),
                NextValue(unpack_lane, 0),
                NextState("UNPACK"),
            ),
        )
        fsm.act("UNPACK",
            row_write.we.eq(1),
            If(unpack_lane == 15,
                If(source_word == SOURCE_WORDS - 1,
                    NextValue(cached_y, source_y),
                    NextValue(cached_valid, 1),
                    NextState("OUTPUT-START"),
                ).Else(
                    NextValue(source_word, source_word + 1),
                    NextState("READ-COMMAND"),
                ),
            ).Else(
                NextValue(unpack_lane, unpack_lane + 1),
            ),
        )
        fsm.act("OUTPUT-START",
            NextValue(output_word, 0),
            NextValue(output_lane, 0),
            NextValue(source_x, display_x),
            NextValue(x_accum, 0),
            NextState("PIXEL-READ"),
        )
        fsm.act("PIXEL-READ",
            row_read.re.eq(1),
            NextState("PIXEL-CAPTURE"),
        )
        fsm.act("PIXEL-CAPTURE",
            NextValue(Array(output_pixels)[output_lane], rgb565),
            If(x_sum >= OUTPUT_WIDTH,
                NextValue(x_accum, x_sum - OUTPUT_WIDTH),
                NextValue(source_x, source_x + 1),
            ).Else(
                NextValue(x_accum, x_sum),
            ),
            If(output_lane == 15,
                NextState("WRITE-COMMAND"),
            ).Else(
                NextValue(output_lane, output_lane + 1),
                NextState("PIXEL-READ"),
            ),
        )
        fsm.act("WRITE-COMMAND",
            port.cmd.valid.eq(1),
            port.cmd.we.eq(1),
            port.cmd.addr.eq(FRAMEBUFFER_NATIVE_BASE +
                output_y * OUTPUT_WORDS + output_word),
            If(port.cmd.ready,
                NextState("WRITE-DATA"),
            ),
        )
        fsm.act("WRITE-DATA",
            port.wdata.valid.eq(1),
            If(port.wdata.ready,
                If(output_word == OUTPUT_WORDS - 1,
                    If(output_y == OUTPUT_HEIGHT - 1,
                        NextValue(completed, completed + 1),
                        NextValue(busy, 0),
                        NextState("IDLE"),
                    ).Else(
                        NextValue(output_y, output_y + 1),
                        If(y_sum >= OUTPUT_HEIGHT,
                            NextValue(y_accum, y_sum - OUTPUT_HEIGHT),
                            NextValue(source_y, source_y + 1),
                            NextValue(cached_valid, 0),
                        ).Else(
                            NextValue(y_accum, y_sum),
                        ),
                        NextState("ROW-CHECK"),
                    ),
                ).Else(
                    NextValue(output_word, output_word + 1),
                    NextValue(output_lane, 0),
                    NextState("PIXEL-READ"),
                ),
            ),
        )
