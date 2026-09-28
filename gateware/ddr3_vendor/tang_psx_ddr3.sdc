// SPDX-License-Identifier: Apache-2.0
//
// Clock definitions follow Sipeed's TangMega-138K-example ddr3_1v4_hs.sdc
// (Apache-2.0, commit 06e7d8b).  The board clock is constrained at its real
// 50 MHz rather than the reference design's 100 MHz, because this design
// clocks the Tang-Control transport from it.  All domains are exclusive; the
// only crossings are the synchronized status snapshot and control levels.

create_clock -name clk50 -period 20 -waveform {0 10} [get_ports {clk}]
create_clock -name clk400 -period 2.5 -waveform {0 1.25} [get_nets {memory_clk}]
create_clock -name sysclk -period 10 -waveform {0 5} [get_pins {u_ddr3/gw3_top/u_ddr_phy_top/fclkdiv/CLKOUT}]
set_clock_groups -exclusive -group [get_clocks {clk400}] -group [get_clocks {clk50}] -group [get_clocks {sysclk}]
