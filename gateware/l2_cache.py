# SPDX-License-Identifier: GPL-3.0-only

"""Write-through L2 cache between the AE350 RAM bridge and the DDR3 arbiters.

An AE350 D-cache miss to DDR3 costs about 570 core cycles (AE350-009), most of
it inside the Gowin controller (core-log entry 43). This cache holds 256-bit
native words, one AE350 cache line each, in block RAM on the AE350's own
native port, so a hit returns two system cycles after the command instead of
going to DDR3.

It is direct-mapped and write-through: every write goes on to DDR3 unchanged
and also updates the line if it is present, so DDR3 is never stale for the
other masters. Only the fabric GPU writes DDR3 behind the AE350, and only in
[bypass_base, bypass_end) (PlayStation VRAM and the HDMI framebuffer), which
reads bypass. With enable low, reads bypass as well while writes still update
present lines, so the cache stays coherent and can be re-enabled at any time.
Tags start invalid from the bitstream.

The upstream port is driven as BurstWishbone2Native drives it: one command at
a time, and a write's data after its command. A miss costs one cycle for the
tag lookup before the DDR3 read.
"""

from migen import If, Memory, Signal

from litex.gen import FSM, LiteXModule, NextState, NextValue


class L2Cache(LiteXModule):
    def __init__(self, upstream, downstream, lines, bypass_base, bypass_end):
        assert upstream.data_width == downstream.data_width
        assert upstream.address_width == downstream.address_width
        assert lines & (lines - 1) == 0
        index_bits = (lines - 1).bit_length()
        address_width = upstream.address_width
        tag_bits = address_width - index_bits
        data_width = upstream.data_width

        self.enable = Signal(reset=1)
        self.read_hits = Signal(32)
        self.read_misses = Signal(32)
        self.bypass_reads = Signal(32)
        self.writes = Signal(32)

        # # #

        data = Memory(data_width, lines)
        tags = Memory(tag_bits + 1, lines, init=[0] * lines)
        data_read = data.get_port()
        data_write = data.get_port(write_capable=True, we_granularity=8)
        tag_read = tags.get_port()
        tag_write = tags.get_port(write_capable=True)
        self.specials += data, tags, data_read, data_write, tag_read, tag_write

        request_we = Signal()
        request_addr = Signal(address_width)
        request_bypass = Signal()
        hit = Signal()
        hit_data = Signal(data_width)
        read_index = Signal(index_bits)

        def index(address):
            return address[:index_bits]

        def tag(address):
            return address[index_bits:]

        def bypassed(address):
            return (address >= bypass_base) & (address < bypass_end)

        self.comb += [
            data_read.adr.eq(read_index),
            tag_read.adr.eq(read_index),
            data_write.adr.eq(index(request_addr)),
            tag_write.adr.eq(index(request_addr)),
            hit.eq(tag_read.dat_r[tag_bits] &
                (tag_read.dat_r[:tag_bits] == tag(request_addr))),
            downstream.flush.eq(upstream.flush),
            upstream.lock.eq(downstream.lock),
        ]

        self.fsm = fsm = FSM(reset_state="IDLE")
        fsm.act("IDLE",
            read_index.eq(index(upstream.cmd.addr)),
            upstream.cmd.ready.eq(1),
            If(upstream.cmd.valid,
                NextValue(request_we, upstream.cmd.we),
                NextValue(request_addr, upstream.cmd.addr),
                NextValue(request_bypass, bypassed(upstream.cmd.addr)),
                If(upstream.cmd.we,
                    NextState("WRITE-CMD"),
                ).Elif(bypassed(upstream.cmd.addr) | ~self.enable,
                    NextState("FILL-CMD"),
                ).Else(
                    NextState("LOOKUP"),
                ),
            ),
        )
        fsm.act("LOOKUP",
            read_index.eq(index(request_addr)),
            If(hit,
                NextValue(hit_data, data_read.dat_r),
                NextValue(self.read_hits, self.read_hits + 1),
                NextState("HIT"),
            ).Else(
                NextState("FILL-CMD"),
            ),
        )
        fsm.act("HIT",
            read_index.eq(index(request_addr)),
            upstream.rdata.valid.eq(1),
            upstream.rdata.data.eq(hit_data),
            upstream.rdata.first.eq(1),
            upstream.rdata.last.eq(1),
            If(upstream.rdata.ready, NextState("IDLE")),
        )
        fsm.act("FILL-CMD",
            read_index.eq(index(request_addr)),
            downstream.cmd.valid.eq(1),
            downstream.cmd.we.eq(0),
            downstream.cmd.addr.eq(request_addr),
            downstream.cmd.first.eq(1),
            downstream.cmd.last.eq(1),
            If(downstream.cmd.ready, NextState("FILL-DATA")),
        )
        fsm.act("FILL-DATA",
            read_index.eq(index(request_addr)),
            downstream.rdata.connect(upstream.rdata),
            If(downstream.rdata.valid & upstream.rdata.ready,
                If(request_bypass | ~self.enable,
                    NextValue(self.bypass_reads, self.bypass_reads + 1),
                ).Else(
                    NextValue(self.read_misses, self.read_misses + 1),
                    data_write.dat_w.eq(downstream.rdata.data),
                    data_write.we.eq(2**(data_width // 8) - 1),
                    tag_write.dat_w.eq((1 << tag_bits) | tag(request_addr)),
                    tag_write.we.eq(1),
                ),
                NextState("IDLE"),
            ),
        )
        fsm.act("WRITE-CMD",
            read_index.eq(index(request_addr)),
            downstream.cmd.valid.eq(1),
            downstream.cmd.we.eq(1),
            downstream.cmd.addr.eq(request_addr),
            downstream.cmd.first.eq(1),
            downstream.cmd.last.eq(1),
            If(downstream.cmd.ready, NextState("WRITE-DATA")),
        )
        fsm.act("WRITE-DATA",
            read_index.eq(index(request_addr)),
            upstream.wdata.connect(downstream.wdata),
            If(upstream.wdata.valid & downstream.wdata.ready,
                NextValue(self.writes, self.writes + 1),
                # A present line takes the written bytes, so it stays current.
                If(hit & ~request_bypass,
                    data_write.dat_w.eq(upstream.wdata.data),
                    data_write.we.eq(upstream.wdata.we),
                ),
                NextState("IDLE"),
            ),
        )
