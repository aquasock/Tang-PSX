`timescale 1ps/1fs

// Exercise the exact X4 DQS/DQ serializer pairing and the one-system-cycle
// write-enable window used by GW5DDRPHY.  The trace is sampled at every DQS
// serializer half-cycle so the eight DQ bits under the driven strobe can be
// compared with the requested byte.
module tb_burst;
    localparam real PCLK_HALF_PS = 6666.666667;
    localparam real FCLK_HALF_PS = 1666.666667;

    reg pclk = 1'b1;
    reg fclk = 1'b1;
    reg reset = 1'b1;
    reg command_write = 1'b0;
    reg trace = 1'b0;
    reg [7:0] dllstep = 8'd0;
    reg [7:0] data_source = 8'h69;
    reg [7:0] data_muxed = 8'd0;
    reg tap0 = 1'b0;
    reg tap1 = 1'b0;
    reg tap2 = 1'b0;

    wire dqsw0;
    wire dqsw270;
    wire dqsr90;
    wire [2:0] rpoint;
    wire [2:0] wpoint;
    wire dq_oe = tap1;
    wire dqs_oe = dq_oe;
    wire dqs_preamble = tap0 & ~tap1;
    wire dqs_postamble = tap2 & ~tap1;
    wire dq_q;
    wire dq_oen;
    wire dqs_q;
    wire dqs_oen;

    integer step;
    integer half_cycle = 0;

    GSR GSR(.GSRI(1'b1));

    always #(PCLK_HALF_PS) pclk = ~pclk;
    always #(FCLK_HALF_PS) fclk = ~fclk;

    always @(posedge pclk) begin
        data_muxed <= data_source;
        tap0 <= command_write;
        tap1 <= tap0;
        tap2 <= tap1;
    end

    DQS #(
        .DQS_MODE("X4"),
        .HWL("false")
    ) dqs (
        .DQSR90(dqsr90), .DQSW0(dqsw0), .DQSW270(dqsw270),
        .RPOINT(rpoint), .WPOINT(wpoint), .RVALID(), .RBURST(),
        .RFLAG(), .WFLAG(), .DQSIN(1'b0), .DLLSTEP(dllstep),
        .WSTEP(8'd0), .READ(4'd0), .RLOADN(1'b1), .RMOVE(1'b0),
        .RDIR(1'b0), .WLOADN(~reset), .WMOVE(1'b0), .WDIR(1'b0),
        .HOLD(1'b0), .RCLKSEL(3'd2), .PCLK(pclk), .FCLK(fclk),
        .RESET(reset)
    );

    OSER8_MEM #(
        .HWL("true"),
        .TCLK_SOURCE("DQSW"),
        .TXCLK_POL(1'b1)
    ) dqs_serializer (
        .Q0(dqs_q), .Q1(dqs_oen),
        .D0(1'b0), .D1(1'b1), .D2(1'b0), .D3(1'b1),
        .D4(1'b0), .D5(1'b1), .D6(1'b0), .D7(1'b1),
        .TX0(~(dqs_oe | dqs_postamble)),
        .TX1(~dqs_oe), .TX2(~dqs_oe),
        .TX3(~(dqs_oe | dqs_preamble)),
        .PCLK(pclk), .FCLK(fclk), .TCLK(dqsw0), .RESET(reset)
    );

    OSER8_MEM #(
        .HWL("true"),
        .TCLK_SOURCE("DQSW270"),
        .TXCLK_POL(1'b0)
    ) dq_serializer (
        .Q0(dq_q), .Q1(dq_oen),
        .D0(data_muxed[0]), .D1(data_muxed[1]),
        .D2(data_muxed[2]), .D3(data_muxed[3]),
        .D4(data_muxed[4]), .D5(data_muxed[5]),
        .D6(data_muxed[6]), .D7(data_muxed[7]),
        .TX0(~(dq_oe | dqs_preamble)),
        .TX1(~(dq_oe | dqs_preamble)),
        .TX2(~(dq_oe | dqs_preamble)),
        .TX3(~(dq_oe | dqs_preamble)),
        .PCLK(pclk), .FCLK(fclk), .TCLK(dqsw270), .RESET(reset)
    );

    always @(dqsw0) begin
        if (trace) begin
            #10;
            $display("half=%0d time_ps=%0t taps=%b%b%b dqs=%b dqs_oen=%b dq=%b dq_oen=%b",
                half_cycle, $time, tap2, tap1, tap0,
                dqs_q, dqs_oen, dq_q, dq_oen);
            half_cycle = half_cycle + 1;
        end
    end

    initial begin
        if (!$value$plusargs("STEP=%d", step))
            step = 0;
        dllstep = step[7:0];

        repeat (8) @(posedge pclk);
        #100 reset = 1'b0;
        repeat (16) @(posedge pclk);

        @(negedge pclk);
        trace = 1'b1;
        command_write = 1'b1;
        @(negedge pclk);
        command_write = 1'b0;
        repeat (6) @(posedge pclk);
        trace = 1'b0;
        $finish;
    end
endmodule
