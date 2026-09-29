# SPDX-License-Identifier: GPL-3.0-only

"""Fair two-client arbiter for the Gate 1 DDR3 native port."""

from migen import FSM, If, Mux, Signal

from litex.gen import LiteXModule, NextState, NextValue
from litex.soc.interconnect import stream


class DDR3RWArbiter(LiteXModule):
    """Serialize two read/write native masters without reordering either.

    A selected write owns the target through its data beat, and a selected
    read owns it through the return beat.  Keeping one transaction in flight
    makes command/data association explicit and lets the existing CPU/video
    arbiter continue to interleave scanout reads while this arbiter waits.
    """

    def __init__(self, first, second, target):
        assert first.data_width == second.data_width == target.data_width
        assert first.address_width == second.address_width == target.address_width
        assert first.mode in ("both", "rw") and second.mode in ("both", "rw")

        owner = Signal()
        prefer_second = Signal(reset=1)
        selected = Signal()
        command_we = Signal()
        command_addr = Signal.like(target.cmd.addr)
        command_first = Signal()
        command_last = Signal()

        self.comb += [
            selected.eq(second.cmd.valid & (~first.cmd.valid | prefer_second)),
            target.flush.eq(first.flush | second.flush),
            first.lock.eq(target.lock),
            second.lock.eq(target.lock),
        ]

        self.fsm = fsm = FSM(reset_state="SELECT")
        fsm.act("SELECT",
            first.cmd.ready.eq(~selected),
            second.cmd.ready.eq(selected),
            If(Mux(selected, second.cmd.valid, first.cmd.valid),
                NextValue(owner, selected),
                NextValue(prefer_second, ~selected),
                NextValue(command_we, Mux(selected,
                    second.cmd.we, first.cmd.we)),
                NextValue(command_addr, Mux(selected,
                    second.cmd.addr, first.cmd.addr)),
                NextValue(command_first, Mux(selected,
                    second.cmd.first, first.cmd.first)),
                NextValue(command_last, Mux(selected,
                    second.cmd.last, first.cmd.last)),
                NextState("ISSUE"),
            ),
        )
        fsm.act("ISSUE",
            target.cmd.valid.eq(1),
            target.cmd.we.eq(command_we),
            target.cmd.addr.eq(command_addr),
            target.cmd.first.eq(command_first),
            target.cmd.last.eq(command_last),
            If(target.cmd.ready,
                If(command_we,
                    NextState("WRITE"),
                ).Else(
                    NextState("READ"),
                ),
            ),
        )
        fsm.act("WRITE",
            target.wdata.valid.eq(Mux(owner, second.wdata.valid, first.wdata.valid)),
            target.wdata.data.eq(Mux(owner, second.wdata.data, first.wdata.data)),
            target.wdata.we.eq(Mux(owner, second.wdata.we, first.wdata.we)),
            target.wdata.first.eq(Mux(owner, second.wdata.first, first.wdata.first)),
            target.wdata.last.eq(Mux(owner, second.wdata.last, first.wdata.last)),
            first.wdata.ready.eq(target.wdata.ready & ~owner),
            second.wdata.ready.eq(target.wdata.ready & owner),
            If(target.wdata.valid & target.wdata.ready,
                NextState("SELECT"),
            ),
        )
        fsm.act("READ",
            first.rdata.valid.eq(target.rdata.valid & ~owner),
            second.rdata.valid.eq(target.rdata.valid & owner),
            first.rdata.data.eq(target.rdata.data),
            second.rdata.data.eq(target.rdata.data),
            first.rdata.first.eq(target.rdata.first),
            first.rdata.last.eq(target.rdata.last),
            second.rdata.first.eq(target.rdata.first),
            second.rdata.last.eq(target.rdata.last),
            target.rdata.ready.eq(Mux(owner, second.rdata.ready, first.rdata.ready)),
            If(target.rdata.valid & target.rdata.ready,
                NextState("SELECT"),
            ),
        )


class DDR3PortArbiter(LiteXModule):
    """Share one in-order native port between the AE350 and video DMA.

    The CPU port can read and write while the video port is read-only.  Read
    commands are tagged as the target accepts them, and the tag FIFO routes
    each in-order response to its owner.  Command arbitration alternates when
    both clients remain active, preventing either the CPU or scanout from
    starving the other.

    A skid buffer registers the command in both directions, so no path runs
    from the target's crossing-FIFO flags to a client's ready, or from a
    client's valid to the target's command enable, within one cycle.  Write
    data may therefore reach the target before its command; the target pairs
    them in order.
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
        self.command_buffer = command_buffer = stream.Buffer(
            target.cmd.description, pipe_valid=True, pipe_ready=True)
        command = command_buffer.sink

        self.comb += [
            command_buffer.source.connect(target.cmd),

            select_video.eq(video.cmd.valid & (~cpu.cmd.valid | prefer_video)),
            selected_we.eq(Mux(select_video, video.cmd.we, cpu.cmd.we)),
            command_allowed.eq(selected_we | owner_fifo.sink.ready),

            command.valid.eq(Mux(select_video, video.cmd.valid, cpu.cmd.valid) &
                command_allowed),
            command.we.eq(selected_we),
            command.addr.eq(Mux(select_video, video.cmd.addr, cpu.cmd.addr)),
            command.first.eq(Mux(select_video, video.cmd.first, cpu.cmd.first)),
            command.last.eq(Mux(select_video, video.cmd.last, cpu.cmd.last)),
            cpu.cmd.ready.eq(command.ready & command_allowed & ~select_video),
            video.cmd.ready.eq(command.ready & command_allowed & select_video),

            read_accepted.eq(command.valid & command.ready & ~selected_we),
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

        self.sync += If(command.valid & command.ready,
            prefer_video.eq(~select_video),
        )
