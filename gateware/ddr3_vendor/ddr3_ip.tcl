# SPDX-License-Identifier: Apache-2.0
#
# Gowin DDR3 Memory Interface configuration for the Tang Console 138K x32
# array.  Values reproduce the ddr3_memory_interface.ipc in Sipeed's
# TangMega-138K-example ddr_memory design (Apache-2.0, commit 06e7d8b) for
# every option supported by the Gowin EDA 1.9.11.03 generator.  Sourced by
# scripts/gen-ddr3-ip.sh after create_ipc; the resulting .ipc is checked
# against ddr3_memory_interface.ipc in this directory.

set_property -dict {
    CONFIG.User_Interface   Controller
    CONFIG.Memory_Clock     400
    CONFIG.CLK_Ratio        1:4
    CONFIG.Dq_Width         32
    CONFIG.Dram_Width       16
    CONFIG.Rank_Address     1
    CONFIG.Row_Address      15
    CONFIG.Column_Address   10
    CONFIG.CAS_Latency      6
    CONFIG.CW_Latency       5
    CONFIG.Additive_Latency 0
    CONFIG.Rtt_Nom          40
    CONFIG.Rtt_Wr           OFF
    CONFIG.Addr_Cmd_Mode    1T
    CONFIG.OUTPUT_DRV       HIGH
    CONFIG.User_Refresh     false
    CONFIG.tRTP_Period      7500
    CONFIG.tRP_Period       15000
    CONFIG.tWTR_Period      7500
    CONFIG.tRC_Period       55000
    CONFIG.tRAS_Period      37500
    CONFIG.tRCD_Period      15000
    CONFIG.tFAW_Period      40000
    CONFIG.tRRD_Period      10000
    CONFIG.tCKE_Period      10000
    CONFIG.tREFI_Period     7800000
    CONFIG.tRFC_Period      260000
    CONFIG.tDLLK            512
} [get_ips DDR3_Memory_Interface_Top]
