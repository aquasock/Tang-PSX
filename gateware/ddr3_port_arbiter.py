# SPDX-License-Identifier: GPL-3.0-only

"""Fair two-client arbiter for the Gate 1 DDR3 native port."""

from migen import If, Mux, Signal

from litex.gen import LiteXModule
from litex.soc.interconnect import stream


class DDR3PortArbiter(LiteXModule):
    """Share one in-order native port between the AE350 and video DMA.

    The CPU port can read and write while the video port is read-only.  Read
    commands are tagged as the target accepts them, and the tag FIFO routes
    each in-order response to its owner.  Command arbitration alternates when
    both clients remain active, preventing either the CPU or scanout from
    starving the other.
    """

    def __init__(self, cpu, video, target, owner_depth=16):
        assert cpu.data_width == video.data_width == target.data_width
        assert cpu.address_width == video.address_width == target.address_width
        assert video.mode in ("r", "read")
        assert owner_depth > 0

        prefer_video = Signal(reset=1)
        select_video = Signal()
        selected_we = Signal()
        command_allowed = Signal()
        read_accepted = Signal()

        self.owner_fifo = owner_fifo = stream.SyncFIFO(
            [("owner", 1)], owner_depth, buffered=True)

        self.comb += [
            select_video.eq(video.cmd.valid & (~cpu.cmd.valid | prefer_video)),
            selected_we.eq(Mux(select_video, video.cmd.we, cpu.cmd.we)),
            command_allowed.eq(selected_we | owner_fifo.sink.ready),

            target.cmd.valid.eq(Mux(select_video, video.cmd.valid, cpu.cmd.valid) &
                command_allowed),
            target.cmd.we.eq(selected_we),
            target.cmd.addr.eq(Mux(select_video, video.cmd.addr, cpu.cmd.addr)),
            target.cmd.first.eq(Mux(select_video, video.cmd.first, cpu.cmd.first)),
            target.cmd.last.eq(Mux(select_video, video.cmd.last, cpu.cmd.last)),
            cpu.cmd.ready.eq(target.cmd.ready & command_allowed & ~select_video),
            video.cmd.ready.eq(target.cmd.ready & command_allowed & select_video),

            read_accepted.eq(target.cmd.valid & target.cmd.ready & ~selected_we),
            owner_fifo.sink.valid.eq(read_accepted),
            owner_fifo.sink.owner.eq(select_video),

            # Only the CPU can write.  The native target may consume write data
            # while accepting the following video command in the same cycle.
            target.wdata.valid.eq(cpu.wdata.valid),
            target.wdata.data.eq(cpu.wdata.data),
            target.wdata.we.eq(cpu.wdata.we),
            target.wdata.first.eq(cpu.wdata.first),
            target.wdata.last.eq(cpu.wdata.last),
            cpu.wdata.ready.eq(target.wdata.ready),
            video.wdata.ready.eq(0),

            cpu.rdata.valid.eq(target.rdata.valid & owner_fifo.source.valid &
                ~owner_fifo.source.owner),
            video.rdata.valid.eq(target.rdata.valid & owner_fifo.source.valid &
                owner_fifo.source.owner),
            cpu.rdata.data.eq(target.rdata.data),
            video.rdata.data.eq(target.rdata.data),
            cpu.rdata.first.eq(target.rdata.first),
            cpu.rdata.last.eq(target.rdata.last),
            video.rdata.first.eq(target.rdata.first),
            video.rdata.last.eq(target.rdata.last),
            target.rdata.ready.eq(owner_fifo.source.valid &
                Mux(owner_fifo.source.owner, video.rdata.ready, cpu.rdata.ready)),
            owner_fifo.source.ready.eq(target.rdata.valid & target.rdata.ready),

            target.flush.eq(cpu.flush | video.flush),
            cpu.lock.eq(target.lock),
            video.lock.eq(target.lock),
        ]

        self.sync += If(target.cmd.valid & target.cmd.ready,
            prefer_video.eq(~select_video),
        )
