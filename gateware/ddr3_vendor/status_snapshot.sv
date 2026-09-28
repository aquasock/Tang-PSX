// SPDX-License-Identifier: GPL-3.0-only
//
// Coherent multi-word status transfer between unrelated clocks.
//
// The destination toggles a request; the source captures its whole status
// vector into a shadow register and returns the toggle.  The destination only
// copies the shadow once the returned toggle has crossed its synchronizer, so
// the shadow is held stable for at least two destination cycles before it is
// sampled and is not rewritten until the next request crosses back.  This is
// the same two-flag handshake as colibri's synchro_handshake, specialized to a
// free-running refresh.  If the source clock is stopped the destination keeps
// its last snapshot and `count` stops advancing.

module status_snapshot #(
    parameter int WIDTH = 32
) (
    input  logic             src_clk,
    input  logic [WIDTH-1:0] src_data,

    input  logic             dst_clk,
    input  logic             dst_rst,
    output logic [WIDTH-1:0] dst_data,
    output logic [31:0]      count
);

    logic             req = 1'b0;
    logic             ack = 1'b0;
    logic [1:0]       req_sync = 2'b00;
    logic [1:0]       ack_sync = 2'b00;
    logic [WIDTH-1:0] shadow;

    always_ff @(posedge src_clk) begin
        req_sync <= {req_sync[0], req};
        if (req_sync[1] != ack) begin
            shadow <= src_data;
            ack    <= req_sync[1];
        end
    end

    always_ff @(posedge dst_clk) begin
        ack_sync <= {ack_sync[0], ack};
        if (ack_sync[1] == req) begin
            dst_data <= shadow;
            req      <= !req;
            count    <= count + 32'd1;
        end
        if (dst_rst) begin
            dst_data <= '0;
            count    <= '0;
        end
    end

endmodule
