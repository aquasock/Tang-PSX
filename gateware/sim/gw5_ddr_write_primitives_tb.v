`timescale 1ps/1fs

// Primitive-level diagnostic for the GW5 X4 DDR write path.  Gowin's GW5A
// OSER8_MEM functional model is identical to the model in the generic IP-core
// simulation library used here.  The GW5A DQS model itself is only a black
// box, while this library contains the behavioral DQS body with the same X4
// interface and HWL behavior.
module tb;
    localparam real PCLK_HALF_PS = 6666.666667;
    localparam real FCLK_HALF_PS = 1666.666667;

    reg pclk = 1'b1;
    reg fclk = 1'b1;
    reg reset = 1'b1;
    reg [7:0] dllstep = 8'd0;
    reg [7:0] data = 8'b0110_1001;

    wire dqsw0;
    wire dqsw270;
    wire dqsr90;
    wire [2:0] rpoint;
    wire [2:0] wpoint;
    wire q_false;
    wire q_true;
    wire oe_false;
    wire oe_true;
    wire effective_tclk = ~dqsw270;

    integer step;
    integer sample;
    integer rotation;
    integer errors_false;
    integer errors_true;
    integer best_false;
    integer best_true;
    integer best_rotation_false;
    integer best_rotation_true;
    reg [63:0] samples_false;
    reg [63:0] samples_true;

    // OSER8_MEM's functional model refers to the global GSR instance by name.
    GSR GSR(.GSRI(1'b1));

    always #(PCLK_HALF_PS) pclk = ~pclk;
    always #(FCLK_HALF_PS) fclk = ~fclk;

    DQS #(
        .DQS_MODE("X4"),
        .HWL("false")
    ) dqs (
        .DQSR90(dqsr90),
        .DQSW0(dqsw0),
        .DQSW270(dqsw270),
        .RPOINT(rpoint),
        .WPOINT(wpoint),
        .RVALID(),
        .RBURST(),
        .RFLAG(),
        .WFLAG(),
        .DQSIN(1'b0),
        .DLLSTEP(dllstep),
        .WSTEP(8'd0),
        .READ(4'd0),
        .RLOADN(1'b1),
        .RMOVE(1'b0),
        .RDIR(1'b0),
        .WLOADN(~reset),
        .WMOVE(1'b0),
        .WDIR(1'b0),
        .HOLD(1'b0),
        .RCLKSEL(3'd2),
        .PCLK(pclk),
        .FCLK(fclk),
        .RESET(reset)
    );

    OSER8_MEM #(
        .HWL("false"),
        .TCLK_SOURCE("DQSW270"),
        .TXCLK_POL(1'b0)
    ) serializer_false (
        .Q0(q_false), .Q1(oe_false),
        .D0(data[0]), .D1(data[1]), .D2(data[2]), .D3(data[3]),
        .D4(data[4]), .D5(data[5]), .D6(data[6]), .D7(data[7]),
        .TX0(1'b0), .TX1(1'b0), .TX2(1'b0), .TX3(1'b0),
        .PCLK(pclk), .FCLK(fclk), .TCLK(dqsw270), .RESET(reset)
    );

    OSER8_MEM #(
        .HWL("true"),
        .TCLK_SOURCE("DQSW270"),
        .TXCLK_POL(1'b0)
    ) serializer_true (
        .Q0(q_true), .Q1(oe_true),
        .D0(data[0]), .D1(data[1]), .D2(data[2]), .D3(data[3]),
        .D4(data[4]), .D5(data[5]), .D6(data[6]), .D7(data[7]),
        .TX0(1'b0), .TX1(1'b0), .TX2(1'b0), .TX3(1'b0),
        .PCLK(pclk), .FCLK(fclk), .TCLK(dqsw270), .RESET(reset)
    );

    initial begin
        if (!$value$plusargs("STEP=%d", step))
            step = 0;
        dllstep = step[7:0];

        // Load DLLSTEP through WLOADN while reset is asserted, then allow the
        // clock-transfer counters to settle before recording a long stream.
        repeat (8) @(posedge pclk);
        #100 reset = 1'b0;
        repeat (24) @(posedge pclk);

        samples_false = 64'd0;
        samples_true = 64'd0;
        for (sample = 0; sample < 64; sample = sample + 1) begin
            @(effective_tclk);
            #10;
            samples_false[sample] = q_false;
            samples_true[sample] = q_true;
        end

        best_false = 65;
        best_true = 65;
        best_rotation_false = -1;
        best_rotation_true = -1;
        for (rotation = 0; rotation < 8; rotation = rotation + 1) begin
            errors_false = 0;
            errors_true = 0;
            for (sample = 0; sample < 64; sample = sample + 1) begin
                if (samples_false[sample] !== data[(sample + rotation) % 8])
                    errors_false = errors_false + 1;
                if (samples_true[sample] !== data[(sample + rotation) % 8])
                    errors_true = errors_true + 1;
            end
            if (errors_false < best_false) begin
                best_false = errors_false;
                best_rotation_false = rotation;
            end
            if (errors_true < best_true) begin
                best_true = errors_true;
                best_rotation_true = rotation;
            end
        end

        $display("step=%0d false_errors=%0d false_rotation=%0d true_errors=%0d true_rotation=%0d false_samples=%016h true_samples=%016h",
            step, best_false, best_rotation_false, best_true, best_rotation_true,
            samples_false, samples_true);
        $finish;
    end
endmodule
