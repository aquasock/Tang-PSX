# SPDX-License-Identifier: GPL-3.0-only

"""Digilent PmodVGA output from a LiteX video stream.

The PmodVGA (rev C.0 schematic, Digilent 500-345) is a dual PMOD: its J1
carries red on pins 1-4 and blue on pins 7-10, its J2 carries green on pins
1-4 and horizontal/vertical sync on pins 7 and 8, and bit 3 of each colour is
the most significant step of its resistor ladder.

Which host socket signal (IO0-IO7) reaches which PMOD pin depends on the
board's numbering. Sipeed's Tang Console numbering is taken to be interleaved,
IO0/2/4/6 on pins 1-4 and IO1/3/5/7 on pins 7-10: TangCore puts one of two
DualShock or SNES controllers on each of those sets. The LiteX convention is
linear, IO0-3 on pins 1-4 and IO4-7 on pins 7-10; the linear order was tried
first on hardware and no placement produced sync.

`VGAOutput` observes (never back-pressures) the stream feeding another video
PHY, so both outputs show the same picture. `mode` selects how the module sits
on the two host sockets, and a built-in pattern that does not depend on the
framebuffer:

    bit 0  J1 on socket b and J2 on socket a (default: J1 on a)
    bit 1  swap each socket's rows (pins 1-4 with 7-10), as when the module
           is plugged in upside down; power and ground stay in place
    bit 2  test pattern: eight colour bars (white, yellow, cyan, green,
           magenta, red, blue, black) over the top three quarters, then
           16-step red, green, blue and grey ramps
    bit 3  linear pin numbering (default: interleaved)

With the wrong placement or numbering the sync signals reach colour inputs and
a monitor finds no signal, so trying the eight settings of bits 0, 1 and 3 with
the pattern on tells a misplaced module from a fault. Syncs are active low (640x480 convention);
colours are zero outside the active area. Everything runs in the stream's
clock domain with two registered stages, the second driving the pins.
"""

from migen import Case, Cat, If, Mux, Signal, Replicate

from litex.gen import LiteXModule


# Logical signals on J pins 1-4, then 7-10.
J1 = ("r0", "r1", "r2", "r3", "b0", "b1", "b2", "b3")
J2 = ("g0", "g1", "g2", "g3", "hs", "vs", None, None)
PLACEMENT_MODES = (0, 1, 2, 3, 8, 9, 10, 11)


def socket_signals(mode):
    """The logical signal on IO0-IO7 of sockets a and b for a mode."""
    first, second = (J2, J1) if mode & 1 else (J1, J2)
    if mode & 2:
        first, second = [s[4:] + s[:4] for s in (first, second)]
    if not mode & 8:
        # Interleaved: IO 2k is J pin k + 1 and IO 2k + 1 is J pin k + 7.
        first, second = [tuple(s[k//2 + 4*(k & 1)] for k in range(8))
                         for s in (first, second)]
    return first, second


class VGAOutput(LiteXModule):
    def __init__(self, sink, pmod_a, pmod_b, h_active=640, v_active=480):
        self.mode = mode = Signal(4)

        # # #

        # Pattern coordinates, counted from the observed data-enable.
        x = Signal(max=h_active + 1)
        y = Signal(max=v_active + 1)
        de_last = Signal()
        self.sync += [
            de_last.eq(sink.de),
            If(sink.de, x.eq(x + 1)).Else(x.eq(0)),
            If(sink.vsync, y.eq(0)).Elif(de_last & ~sink.de, y.eq(y + 1)),
        ]

        def steps(value, count, start, width):
            """How many of `count` equal-width steps after `start` `value` has entered."""
            return sum(value >= start + k*width for k in range(1, count))

        bar = Signal(3)
        level = Signal(4)
        band = Signal(2)
        ramps_start = v_active*3//4
        self.comb += [
            bar.eq(steps(x, 8, 0, h_active//8)),
            level.eq(steps(x, 16, 0, h_active//16)),
            band.eq(steps(y, 4, ramps_start, (v_active - ramps_start)//4)),
        ]
        pattern = {c: Signal(4) for c in "rgb"}
        # Ramp bands: red, green, blue, then all three (grey).
        self.comb += If(y < ramps_start,
            # White, yellow, cyan, green, magenta, red, blue, black.
            pattern["r"].eq(Replicate(~bar[1], 4)),
            pattern["g"].eq(Replicate(~bar[2], 4)),
            pattern["b"].eq(Replicate(~bar[0], 4)),
        ).Else(
            pattern["r"].eq(Mux((band == 0) | (band == 3), level, 0)),
            pattern["g"].eq(Mux((band == 1) | (band == 3), level, 0)),
            pattern["b"].eq(Mux((band == 2) | (band == 3), level, 0)),
        )

        # Stage 1: logical signals.
        logical = {name: Signal(name=f"vga_{name}") for name in (*J1, *J2) if name}
        for c in "rgb":
            data = Signal(4)
            self.comb += data.eq(Mux(mode[2], pattern[c], getattr(sink, c)[4:8]))
            self.sync += [logical[f"{c}{i}"].eq(data[i] & sink.de) for i in range(4)]
        self.sync += [
            logical["hs"].eq(~sink.hsync),
            logical["vs"].eq(~sink.vsync),
        ]

        # Stage 2: place them on the sockets.
        placement = Signal(3)
        self.comb += placement.eq(Cat(mode[:2], mode[3]))
        self.sync += Case(placement, {
            index: [
                pads[i].eq(logical[name] if name else 0)
                for pads, names in zip((pmod_a, pmod_b), socket_signals(mode_bits))
                for i, name in enumerate(names)
            ]
            for index, mode_bits in enumerate(PLACEMENT_MODES)
        })

