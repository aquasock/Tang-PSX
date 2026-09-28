// SPDX-License-Identifier: GPL-3.0-only
//
// Full-array pattern test for the Gowin DDR3 controller's native port.
//
// Each pass writes every BL8 burst in the array, then reads the array back in
// the same order and compares every byte.  Burst data comes from eight
// independent xorshift32 generators that restart at the same seed for the
// write and read sweeps, so the expected word is regenerated rather than
// stored.  Odd passes reuse the preceding seed and write the bitwise
// complement, which forces every DRAM cell to toggle between consecutive
// passes; the seed then advances so stale contents from an earlier pass cannot
// match.
//
// Commands are only issued in the cycle where the controller reports ready, and
// write data is presented in the same cycle as its write command.  Byte i of
// the 256-bit native word is flagged by bit i of the error masks.

module ddr3_tester #(
    parameter int BURST_BITS = 25   // 2^25 bursts * 32 bytes = 1 GiB
) (
    input  logic         clk,
    input  logic         rst,
    input  logic         calib_done,

    // Gowin DDR3 native (controller) interface.
    input  logic         cmd_ready,
    output logic [2:0]   cmd,
    output logic         cmd_en,
    output logic [28:0]  addr,
    input  logic         wr_data_rdy,
    output logic [255:0] wr_data,
    output logic         wr_data_en,
    output logic         wr_data_end,
    output logic [31:0]  wr_data_mask,
    input  logic [255:0] rd_data,
    input  logic         rd_data_valid,

    // Status, sampled by the diagnostic snapshot.
    output logic [31:0]  flags,
    output logic [31:0]  pass_count,
    output logic [31:0]  error_passes,
    output logic [31:0]  progress,
    output logic [31:0]  error_bursts,
    output logic [31:0]  error_mask,
    output logic [31:0]  first_error_burst,
    output logic [31:0]  first_error_pass,
    output logic [31:0]  first_error_mask,
    output logic [31:0]  last_error_burst,
    output logic [31:0]  write_cycles,
    output logic [31:0]  read_cycles,
    output logic [31:0]  verified_bursts,
    output logic [31:0]  stall_events,
    output logic [31:0]  heartbeat,
    output logic [255:0] first_error_expected,
    output logic [255:0] first_error_observed
);

    localparam logic [2:0] CMD_WRITE = 3'b000;
    localparam logic [2:0] CMD_READ  = 3'b001;
    localparam logic [BURST_BITS:0] BURST_COUNT = 1 << BURST_BITS;
    localparam int STALL_BITS = 26;  // ~0.67 s at 100 MHz without a read response

    function automatic logic [31:0] seed_base(input int i);
        case (i)
            0:       return 32'h5883adb4;
            1:       return 32'h11223344;
            2:       return 32'h99aabbcc;
            3:       return 32'h0f1e2d3c;
            4:       return 32'hc88ad596;
            5:       return 32'h55667788;
            6:       return 32'hddeeff01;
            default: return 32'haf5d632f;
        endcase
    endfunction

    typedef enum logic [1:0] {
        S_IDLE  = 2'd0,
        S_WRITE = 2'd1,
        S_READ  = 2'd2
    } state_t;

    function automatic logic [31:0] xorshift32(input logic [31:0] x);
        logic [31:0] y;
        y = x ^ (x << 13);
        y = y ^ (y >> 17);
        return y ^ (y << 5);
    endfunction

    function automatic logic [255:0] step_all(input logic [255:0] s);
        logic [255:0] n;
        for (int i = 0; i < 8; i++)
            n[32*i +: 32] = xorshift32(s[32*i +: 32]);
        return n;
    endfunction

    function automatic logic [255:0] seed_all(input logic [31:0] pass_seed);
        logic [255:0] s;
        logic [31:0] w;
        for (int i = 0; i < 8; i++) begin
            w = seed_base(i) ^ pass_seed;
            s[32*i +: 32] = (w == 32'd0) ? seed_base(i) : w;
        end
        return s;
    endfunction

    state_t               state;
    logic                 calib_q;
    logic [BURST_BITS:0]  issued;
    logic [BURST_BITS:0]  returned;
    logic [31:0]          pass_seed;
    logic [31:0]          pass_seed_next;
    logic [255:0]         seed_cur;
    logic [255:0]         seed_next;
    logic                 invert;
    logic [255:0]         wprng;
    logic [255:0]         rprng;
    logic                 pass_error;
    logic                 stalled;
    logic [STALL_BITS-1:0] idle_count;
    logic [31:0]          phase_cycles;

    // Read-compare pipeline.
    logic                 v0, v1;
    logic [255:0]         rd0, exp0, rd1, exp1;
    logic [BURST_BITS-1:0] idx0, idx1;
    logic [31:0]          byte_err1;
    logic                 first_valid;

    wire issue_write = (state == S_WRITE) && (issued != BURST_COUNT) && cmd_ready && wr_data_rdy;
    wire issue_read  = (state == S_READ)  && (issued != BURST_COUNT) && cmd_ready;
    wire read_done   = (state == S_READ)  && (returned == BURST_COUNT) && !v0 && !v1;

    assign cmd          = (state == S_WRITE) ? CMD_WRITE : CMD_READ;
    assign cmd_en       = issue_write || issue_read;
    assign addr         = {1'b0, issued[BURST_BITS-1:0], 3'b000};
    assign wr_data      = wprng ^ {256{invert}};
    assign wr_data_en   = issue_write;
    assign wr_data_end  = issue_write;
    assign wr_data_mask = '0;

    assign flags    = {19'd0, first_valid, stalled, pass_error, (pass_count != 0), invert,
                       6'd0, state};
    assign progress = 32'((state == S_READ) ? returned : issued);

    // pass_seed only changes at a pass boundary, so the seeded generator
    // states are pipelined off the transition path.
    always_ff @(posedge clk) begin
        pass_seed_next <= pass_seed + 32'h9e3779b9;
        seed_cur       <= seed_all(pass_seed);
        seed_next      <= seed_all(pass_seed_next);
    end

    always_ff @(posedge clk) begin
        calib_q   <= calib_done;
        heartbeat <= heartbeat + 32'd1;

        v0 <= 1'b0;
        if (rd_data_valid) begin
            v0       <= 1'b1;
            rd0      <= rd_data;
            exp0     <= rprng ^ {256{invert}};
            idx0     <= returned[BURST_BITS-1:0];
            rprng    <= step_all(rprng);
            returned <= returned + 1'b1;
        end

        v1   <= v0;
        rd1  <= rd0;
        exp1 <= exp0;
        idx1 <= idx0;
        for (int i = 0; i < 32; i++)
            byte_err1[i] <= v0 && (rd0[8*i +: 8] != exp0[8*i +: 8]);

        if (v1) begin
            verified_bursts <= verified_bursts + 32'd1;
            if (byte_err1 != 32'd0) begin
                pass_error       <= 1'b1;
                error_bursts     <= error_bursts + 32'd1;
                error_mask       <= error_mask | byte_err1;
                last_error_burst <= 32'(idx1);
                if (!first_valid) begin
                    first_valid          <= 1'b1;
                    first_error_burst    <= 32'(idx1);
                    first_error_pass     <= pass_count;
                    first_error_mask     <= byte_err1;
                    first_error_expected <= exp1;
                    first_error_observed <= rd1;
                end
            end
        end

        if (state != S_IDLE)
            phase_cycles <= phase_cycles + 32'd1;

        if (issue_write) begin
            issued <= issued + 1'b1;
            wprng  <= step_all(wprng);
        end
        if (issue_read)
            issued <= issued + 1'b1;

        // A read response that never arrives would otherwise hang silently.
        if (state == S_READ && issued != returned && !rd_data_valid) begin
            idle_count <= idle_count + 1'b1;
            if (&idle_count) begin
                stalled      <= 1'b1;
                stall_events <= stall_events + 32'd1;
            end
        end else begin
            idle_count <= '0;
        end

        unique case (state)
            S_IDLE: begin
                if (calib_q) begin
                    state        <= S_WRITE;
                    issued       <= '0;
                    wprng        <= seed_cur;
                    phase_cycles <= '0;
                end
            end
            S_WRITE: begin
                if (issued == BURST_COUNT) begin
                    state        <= S_READ;
                    issued       <= '0;
                    returned     <= '0;
                    rprng        <= seed_cur;
                    write_cycles <= phase_cycles;
                    phase_cycles <= '0;
                end
            end
            S_READ: begin
                if (read_done) begin
                    read_cycles  <= phase_cycles;
                    phase_cycles <= '0;
                    pass_count   <= pass_count + 32'd1;
                    if (pass_error)
                        error_passes <= error_passes + 32'd1;
                    pass_error   <= 1'b0;
                    if (invert)
                        pass_seed <= pass_seed_next;
                    invert       <= !invert;
                    state        <= S_WRITE;
                    issued       <= '0;
                    wprng        <= invert ? seed_next : seed_cur;
                end
            end
            default: state <= S_IDLE;
        endcase

        if (rst) begin
            state             <= S_IDLE;
            calib_q           <= 1'b0;
            issued            <= '0;
            returned          <= '0;
            pass_seed         <= '0;
            invert            <= 1'b0;
            pass_error        <= 1'b0;
            stalled           <= 1'b0;
            idle_count        <= '0;
            phase_cycles      <= '0;
            v0                <= 1'b0;
            v1                <= 1'b0;
            first_valid       <= 1'b0;
            heartbeat         <= '0;
            pass_count        <= '0;
            error_passes      <= '0;
            error_bursts      <= '0;
            error_mask        <= '0;
            first_error_burst <= '0;
            first_error_pass  <= '0;
            first_error_mask  <= '0;
            last_error_burst  <= '0;
            write_cycles      <= '0;
            read_cycles       <= '0;
            verified_bursts   <= '0;
            stall_events      <= '0;
        end
    end

endmodule
