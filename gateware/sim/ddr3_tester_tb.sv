// SPDX-License-Identifier: GPL-3.0-only
//
// Self-checking test of ddr3_tester and status_snapshot against a behavioral
// model of the Gowin DDR3 native port: random command/write readiness, in-order
// reads with random latency, and protocol assertions.  A single corrupted byte
// is injected into one read response after the first clean passes.
//
//   $ verilator --binary --timing --top-module tb_ddr3_tester -Mdir build/sim/ddr3_tester \
//       gateware/sim/ddr3_tester_tb.sv gateware/ddr3_vendor/ddr3_tester.sv \
//       gateware/ddr3_vendor/status_snapshot.sv
//   $ build/sim/ddr3_tester/Vtb_ddr3_tester

`timescale 1ns/1ps

module tb_ddr3_tester;
    localparam int BURST_BITS  = 6;
    localparam int BURSTS      = 1 << BURST_BITS;
    localparam int FAULT_PASS  = 3;
    localparam int FAULT_BURST = 37;
    localparam int FAULT_BYTE  = 13;

    logic clk = 1'b0, dst_clk = 1'b0;
    always #5 clk = !clk;        // 100 MHz controller clock
    always #10.3 dst_clk = !dst_clk;  // unrelated ~48.5 MHz diagnostic clock

    logic rst = 1'b1, calib = 1'b0;
    logic cmd_ready, wr_data_rdy;
    logic [2:0] cmd;
    logic cmd_en, wr_data_en, wr_data_end;
    logic [28:0] addr;
    logic [255:0] wr_data, rd_data;
    logic [31:0] wr_data_mask;
    logic rd_data_valid;

    logic [31:0] flags, pass_count, error_passes, progress, error_bursts, error_mask;
    logic [31:0] first_burst, first_pass, first_mask, last_burst, write_cycles, read_cycles;
    logic [31:0] verified, stalls, heartbeat;
    logic [255:0] first_expected, first_observed;

    ddr3_tester #(.BURST_BITS(BURST_BITS)) dut (
        .clk(clk), .rst(rst), .calib_done(calib),
        .cmd_ready(cmd_ready), .cmd(cmd), .cmd_en(cmd_en), .addr(addr),
        .wr_data_rdy(wr_data_rdy), .wr_data(wr_data), .wr_data_en(wr_data_en),
        .wr_data_end(wr_data_end), .wr_data_mask(wr_data_mask),
        .rd_data(rd_data), .rd_data_valid(rd_data_valid),
        .flags(flags), .pass_count(pass_count), .error_passes(error_passes),
        .progress(progress), .error_bursts(error_bursts), .error_mask(error_mask),
        .first_error_burst(first_burst), .first_error_pass(first_pass),
        .first_error_mask(first_mask), .last_error_burst(last_burst),
        .write_cycles(write_cycles), .read_cycles(read_cycles),
        .verified_bursts(verified), .stall_events(stalls), .heartbeat(heartbeat),
        .first_error_expected(first_expected), .first_error_observed(first_observed)
    );

    logic [63:0] snap;
    logic [31:0] snap_count;
    status_snapshot #(.WIDTH(64)) snapshot (
        .src_clk(clk), .src_data({heartbeat, pass_count}),
        .dst_clk(dst_clk), .dst_rst(1'b0), .dst_data(snap), .count(snap_count)
    );

    // ---- Behavioral controller -------------------------------------------
    logic [255:0] memory [BURSTS];
    logic [255:0] last_write0 [8];   // burst-0 data per pass, for toggle check
    logic [31:0]  lfsr = 32'h1;
    int           errors = 0;
    int           writes_in_pass = 0;

    typedef struct { logic [255:0] data; int due; int burst; } read_t;
    read_t queue [$];
    int cycle = 0;
    int last_due = 0;

    always_ff @(posedge clk) begin
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
    end
    assign cmd_ready   = lfsr[3] | lfsr[7];      // ~75% ready
    assign wr_data_rdy = lfsr[11] | lfsr[13];

    always @(posedge clk) begin
        cycle <= cycle + 1;
        rd_data_valid <= 1'b0;

        if (!rst) begin
            if (cmd_en && !cmd_ready) begin
                $display("FAIL: command issued while cmd_ready low"); errors++;
            end
            if (wr_data_en !== (cmd_en && cmd == 3'b000)) begin
                $display("FAIL: write data not paired with write command"); errors++;
            end
            if (wr_data_en && !wr_data_rdy) begin
                $display("FAIL: write data issued while wr_data_rdy low"); errors++;
            end
            if (wr_data_en && (wr_data_end !== 1'b1 || wr_data_mask !== '0)) begin
                $display("FAIL: bad write end/mask"); errors++;
            end
            if (cmd_en && (addr[2:0] != 0 || addr[28:3] >= BURSTS)) begin
                $display("FAIL: bad address %h", addr); errors++;
            end
        end

        if (cmd_en && cmd_ready && cmd == 3'b000) begin
            memory[addr[28:3]] <= wr_data;
            if (addr[28:3] == 0 && pass_count < 8)
                last_write0[pass_count] = wr_data;
        end
        if (cmd_en && cmd_ready && cmd == 3'b001) begin
            read_t r;
            r.data  = memory[addr[28:3]];
            r.burst = addr[28:3];
            // In-order return: never earlier than the previous response.
            r.due   = cycle + 4 + (lfsr[20:16] % 17);
            if (r.due <= last_due) r.due = last_due + 1;
            last_due = r.due;
            if (pass_count == FAULT_PASS && r.burst == FAULT_BURST)
                r.data[8*FAULT_BYTE +: 8] = r.data[8*FAULT_BYTE +: 8] ^ 8'h10;
            queue.push_back(r);
        end
        if (queue.size() != 0 && queue[0].due <= cycle) begin
            rd_data       <= queue[0].data;
            rd_data_valid <= 1'b1;
            void'(queue.pop_front());
        end
    end

    // ---- Snapshot coherence: heartbeat and pass count never tear ---------
    logic [31:0] prev_hb = 0, prev_pass = 0;
    always @(posedge dst_clk) begin
        if (snap_count > 2) begin
            if (snap[63:32] < prev_hb || snap[31:0] < prev_pass) begin
                $display("FAIL: snapshot went backwards"); errors++;
            end
            prev_hb   <= snap[63:32];
            prev_pass <= snap[31:0];
        end
    end

    initial begin
        repeat (10) @(posedge clk);
        rst <= 1'b0;
        repeat (20) @(posedge clk);
        calib <= 1'b1;

        wait (pass_count == 6);
        @(posedge clk);

        if (error_passes != 1) begin
            $display("FAIL: error_passes=%0d, expected 1", error_passes); errors++;
        end
        if (error_bursts != 1 || first_burst != FAULT_BURST || last_burst != FAULT_BURST) begin
            $display("FAIL: error_bursts=%0d first=%0d last=%0d", error_bursts, first_burst, last_burst);
            errors++;
        end
        if (first_pass != FAULT_PASS || first_mask != (32'd1 << FAULT_BYTE) || error_mask != first_mask) begin
            $display("FAIL: first_pass=%0d first_mask=%h error_mask=%h", first_pass, first_mask, error_mask);
            errors++;
        end
        if ((first_expected ^ first_observed) != (256'h10 << (8*FAULT_BYTE))) begin
            $display("FAIL: captured expected/observed differ by the wrong bits"); errors++;
        end
        if (verified != 6 * BURSTS) begin
            $display("FAIL: verified=%0d, expected %0d", verified, 6 * BURSTS); errors++;
        end
        // Passes 0/1, 2/3, 4/5 share seeds and complement; the pairs differ.
        for (int p = 0; p < 6; p += 2) begin
            if (last_write0[p + 1] !== ~last_write0[p]) begin
                $display("FAIL: pass %0d does not complement pass %0d", p + 1, p); errors++;
            end
        end
        if (last_write0[2] === last_write0[0] || last_write0[4] === last_write0[2]) begin
            $display("FAIL: seed did not advance between pass pairs"); errors++;
        end
        if (stalls != 0 || flags[11]) begin
            $display("FAIL: stall reported"); errors++;
        end
        if (snap_count < 10) begin
            $display("FAIL: snapshot handshake not running (%0d)", snap_count); errors++;
        end

        if (errors == 0)
            $display("PASS: %0d passes, %0d bursts verified, fault at burst %0d byte %0d detected; write %0d / read %0d cycles",
                pass_count, verified, first_burst, FAULT_BYTE, write_cycles, read_cycles);
        else
            $display("FAIL: %0d errors", errors);
        $finish;
    end

    initial begin
        #5ms;
        $display("FAIL: timeout (pass_count=%0d state=%0d progress=%0d)", pass_count, flags[1:0], progress);
        $finish;
    end
endmodule
