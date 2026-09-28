# SPDX-License-Identifier: GPL-3.0-only

"""Tang-Control stream receiver for loading AE350 programs.

iosys_bl616 delivers a Tang-Control stream (tangctl.py stream <file>) as a byte stream with
single-cycle start, end, and cancel strobes in its own clock domain. StreamLoader packs the bytes
into little-endian 32-bit words and passes them, with the start/end/cancel events in stream
order, through a clock-domain-crossing FIFO to CSRs that firmware drains:

    status  bit 0 valid (an entry is available), bits 2:1 tag, bit 3 overflow (sticky)
    data    DATA: packed word; END: total byte count; START/CANCEL: 0
    pop     write any value to discard the current entry

The stream is held (stream_ready low) whenever the FIFO cannot take the next word or a latched
event is still waiting, so no byte is dropped. The overflow bit reports an event strobe that
arrived while the previous one of the same kind was still pending, which the Tang-Control
stop-and-wait protocol should never produce. Session, byte, end, and cancel counters in the
stream domain follow Tang-Phosphor's stream diagnostics.
"""

from migen import If, Signal
from migen.genlib.cdc import MultiReg

from litex.gen import LiteXModule
from litex.soc.interconnect import stream
from litex.soc.interconnect.csr import AutoCSR, CSR, CSRField, CSRStatus

TAG_DATA   = 0
TAG_START  = 1
TAG_END    = 2
TAG_CANCEL = 3


class StreamLoader(LiteXModule, AutoCSR):
    def __init__(self, stream_domain="diag", depth=512):
        # Stream side (stream_domain), driven by iosys_bl616.
        self.start  = Signal()
        self.end    = Signal()
        self.cancel = Signal()
        self.data   = Signal(8)
        self.valid  = Signal()
        self.ready  = Signal()

        # Stream-domain diagnostics.
        self.sessions = Signal(32)
        self.bytes    = Signal(32)
        self.ends     = Signal(32)
        self.cancels  = Signal(32)
        self.overflow = Signal()

        self._status = CSRStatus(fields=[
            CSRField("valid",    size=1, description="An entry is available"),
            CSRField("tag",      size=2, description="0 data, 1 start, 2 end, 3 cancel"),
            CSRField("overflow", size=1, description="An event strobe was lost (sticky)"),
        ])
        self._data = CSRStatus(32, description="Current entry: data word, or byte count for END")
        self._pop  = CSR()  # write any value to discard the current entry

        # # #

        layout = [("tag", 2), ("data", 32)]
        self.cdc = cdc = stream.ClockDomainCrossing(layout,
            cd_from=stream_domain, cd_to="sys", depth=depth)
        sink = cdc.sink

        word        = Signal(32)
        byte_index  = Signal(2)
        byte_count  = Signal(32)
        start_p     = Signal()
        end_p       = Signal()
        cancel_p    = Signal()
        end_flush   = Signal()
        end_word    = Signal(32)
        end_count   = Signal(32)
        overflow    = self.overflow
        next_word   = Signal(32)
        word_done   = Signal()
        push        = Signal()

        sync = getattr(self.sync, stream_domain)

        # Data is only accepted while no event waits and the FIFO has room. Tang-Control sends
        # a session's END only after its last byte is taken, but may open the next session
        # before that END has been queued, so END/CANCEL are snapshotted when they arrive and
        # are queued ahead of the next START.
        self.comb += [
            self.ready.eq(sink.ready & ~start_p & ~end_p & ~cancel_p),
            next_word.eq(word),
            word_done.eq(self.valid & self.ready & (byte_index == 3)),
            push.eq(~word_done & sink.ready),
        ]
        for index in range(4):
            self.comb += If(byte_index == index,
                next_word[8*index:8*(index + 1)].eq(self.data))

        # FIFO priority: a completed word, the pending session's partial word, END, CANCEL,
        # then the next session's START.
        self.comb += [
            sink.valid.eq(0),
            If(word_done,
                sink.valid.eq(1), sink.tag.eq(TAG_DATA), sink.data.eq(next_word),
            ).Elif(end_p & end_flush,
                sink.valid.eq(1), sink.tag.eq(TAG_DATA), sink.data.eq(end_word),
            ).Elif(end_p,
                sink.valid.eq(1), sink.tag.eq(TAG_END), sink.data.eq(end_count),
            ).Elif(cancel_p,
                sink.valid.eq(1), sink.tag.eq(TAG_CANCEL), sink.data.eq(0),
            ).Elif(start_p,
                sink.valid.eq(1), sink.tag.eq(TAG_START), sink.data.eq(0),
            ),
        ]

        sync += [
            If(self.start,  self.sessions.eq(self.sessions + 1)),
            If(self.valid & self.ready, self.bytes.eq(self.bytes + 1)),
            If(self.end,    self.ends.eq(self.ends + 1)),
            If(self.cancel, self.cancels.eq(self.cancels + 1)),
        ]
        sync += [
            If(self.valid & self.ready,
                word.eq(next_word),
                byte_index.eq(byte_index + 1),
                byte_count.eq(byte_count + 1),
                If(byte_index == 3, word.eq(0)),
            ),
            If(push,
                If(end_p & end_flush,
                    end_flush.eq(0),
                ).Elif(end_p,
                    end_p.eq(0),
                ).Elif(cancel_p,
                    cancel_p.eq(0),
                ).Elif(start_p,
                    start_p.eq(0),
                    word.eq(0),
                    byte_index.eq(0),
                    byte_count.eq(0),
                ),
            ),
            If(self.end,
                If(end_p, overflow.eq(1)),
                end_p.eq(1),
                end_flush.eq(byte_index != 0),
                end_word.eq(word),
                end_count.eq(byte_count),
            ),
            If(self.cancel,
                If(cancel_p, overflow.eq(1)),
                cancel_p.eq(1),
                word.eq(0),
                byte_index.eq(0),
            ),
            If(self.start,
                If(start_p, overflow.eq(1)),
                start_p.eq(1),
            ),
        ]

        # CPU side (sys).
        overflow_sys = Signal()
        self.specials += MultiReg(overflow, overflow_sys, "sys")
        source = cdc.source
        self.comb += [
            self._status.fields.valid.eq(source.valid),
            self._status.fields.tag.eq(source.tag),
            self._status.fields.overflow.eq(overflow_sys),
            self._data.status.eq(source.data),
            source.ready.eq(self._pop.re),
        ]
