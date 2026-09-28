// SPDX-License-Identifier: GPL-3.0-only
//
// LiteDRAM native-port front end for the Gowin DDR3 controller.
//
// The LiteDRAM port carries a command stream and a separate write-data stream
// that may follow its command by any number of cycles.  The Gowin controller
// needs each write command and its data in the same cycle, so one command is
// held here until it can be issued, together with its data for a write.  The
// controller's read data cannot be back-pressured; reads are only issued
// while the local return FIFO has a guaranteed free entry.
//
// Addresses on the port are in 256-bit words.  LiteDRAM byte enables are
// active-high while Gowin's write mask marks bytes to skip.

module gowin_ddr3_native #(
    parameter int ADDR_BITS  = 25,  // 1 GiB / 32 bytes
    parameter int READ_DEPTH = 8
) (
    input  logic                 clk,
    input  logic                 rst,

    // LiteDRAM native port (slave side).
    input  logic                 cmd_valid,
    output logic                 cmd_ready,
    input  logic                 cmd_we,
    input  logic [ADDR_BITS-1:0] cmd_addr,
    input  logic                 wdata_valid,
    output logic                 wdata_ready,
    input  logic [255:0]         wdata_data,
    input  logic [31:0]          wdata_we,
    output logic                 rdata_valid,
    input  logic                 rdata_ready,
    output logic [255:0]         rdata_data,

    // Gowin DDR3 controller.
    input  logic                 ctrl_cmd_ready,
    output logic [2:0]           ctrl_cmd,
    output logic                 ctrl_cmd_en,
    output logic [28:0]          ctrl_addr,
    input  logic                 ctrl_wr_data_rdy,
    output logic [255:0]         ctrl_wr_data,
    output logic                 ctrl_wr_data_en,
    output logic                 ctrl_wr_data_end,
    output logic [31:0]          ctrl_wr_data_mask,
    input  logic [255:0]         ctrl_rd_data,
    input  logic                 ctrl_rd_data_valid,

    // Diagnostics.
    output logic [31:0]          reads,
    output logic [31:0]          writes,
    output logic                 overflow
);

    localparam int PTR_BITS = $clog2(READ_DEPTH);

    logic                 pend_valid;
    logic                 pend_we;
    logic [ADDR_BITS-1:0] pend_addr;

    // Reads issued to the controller and not yet taken from the return FIFO.
    logic [PTR_BITS:0]    in_flight;

    logic [255:0]         fifo_data [READ_DEPTH];
    logic [PTR_BITS-1:0]  fifo_wr, fifo_rd;
    logic [PTR_BITS:0]    fifo_count;

    wire read_credit = in_flight != READ_DEPTH[PTR_BITS:0];
    wire issue_write = pend_valid &&  pend_we && ctrl_cmd_ready && ctrl_wr_data_rdy && wdata_valid;
    wire issue_read  = pend_valid && !pend_we && ctrl_cmd_ready && read_credit;
    wire issue       = issue_write || issue_read;
    wire rdata_fire  = rdata_valid && rdata_ready;

    assign cmd_ready         = !pend_valid || issue;
    assign wdata_ready       = issue_write;

    assign ctrl_cmd          = pend_we ? 3'b000 : 3'b001;
    assign ctrl_cmd_en       = issue;
    assign ctrl_addr         = 29'({pend_addr, 3'b000});
    assign ctrl_wr_data      = wdata_data;
    assign ctrl_wr_data_en   = issue_write;
    assign ctrl_wr_data_end  = issue_write;
    assign ctrl_wr_data_mask = ~wdata_we;

    assign rdata_valid       = fifo_count != 0;
    assign rdata_data        = fifo_data[fifo_rd];

    always_ff @(posedge clk) begin
        if (cmd_valid && cmd_ready) begin
            pend_valid <= 1'b1;
            pend_we    <= cmd_we;
            pend_addr  <= cmd_addr;
        end else if (issue) begin
            pend_valid <= 1'b0;
        end

        in_flight <= in_flight + {{PTR_BITS{1'b0}}, issue_read} - {{PTR_BITS{1'b0}}, rdata_fire};

        if (ctrl_rd_data_valid) begin
            fifo_data[fifo_wr] <= ctrl_rd_data;
            fifo_wr            <= fifo_wr + 1'b1;
            if (fifo_count == READ_DEPTH[PTR_BITS:0] && !rdata_fire)
                overflow <= 1'b1;
        end
        if (rdata_fire)
            fifo_rd <= fifo_rd + 1'b1;
        fifo_count <= fifo_count + {{PTR_BITS{1'b0}}, ctrl_rd_data_valid}
                                 - {{PTR_BITS{1'b0}}, rdata_fire};

        if (issue_read)
            reads <= reads + 32'd1;
        if (issue_write)
            writes <= writes + 32'd1;

        if (rst) begin
            pend_valid <= 1'b0;
            in_flight  <= '0;
            fifo_wr    <= '0;
            fifo_rd    <= '0;
            fifo_count <= '0;
            reads      <= '0;
            writes     <= '0;
            overflow   <= 1'b0;
        end
    end

endmodule
