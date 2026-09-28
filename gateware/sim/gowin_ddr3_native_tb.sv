// SPDX-License-Identifier: GPL-3.0-only
//
// Self-checking test of gowin_ddr3_native.  A randomized LiteDRAM-style
// master sends commands and, for writes, byte-masked data a random number of
// cycles later, and stalls read data at random.  A behavioral Gowin
// controller model has random command/write readiness, applies Gowin's
// active-high write mask, and returns reads in order after a random latency
// without accepting back-pressure.  Every read is checked against a
// reference memory.
//
//   $ verilator --binary --timing --top-module tb_gowin_ddr3_native -Mdir build/sim/gowin_ddr3_native \
//       gateware/sim/gowin_ddr3_native_tb.sv gateware/ddr3_vendor/gowin_ddr3_native.sv
//   $ build/sim/gowin_ddr3_native/Vtb_gowin_ddr3_native

`timescale 1ns/1ps

module tb_gowin_ddr3_native;
    localparam int ADDR_BITS = 25;
    localparam int WORDS     = 16;   // small address set forces read-after-write hazards
    localparam int OPS       = 20000;

    logic clk = 1'b0;
    always #5 clk = !clk;
    logic rst = 1'b1;

    logic                 cmd_valid = 1'b0, cmd_ready, cmd_we = 1'b0;
    logic [ADDR_BITS-1:0] cmd_addr = '0;
    logic                 wdata_valid = 1'b0, wdata_ready;
    logic [255:0]         wdata_data = '0;
    logic [31:0]          wdata_we = '0;
    logic                 rdata_valid, rdata_ready;
    logic [255:0]         rdata_data;

    logic         ctrl_cmd_ready, ctrl_cmd_en, ctrl_wr_data_rdy;
    logic [2:0]   ctrl_cmd;
    logic [28:0]  ctrl_addr;
    logic [255:0] ctrl_wr_data, ctrl_rd_data;
    logic         ctrl_wr_data_en, ctrl_wr_data_end, ctrl_rd_data_valid;
    logic [31:0]  ctrl_wr_data_mask;
    logic [31:0]  reads, writes;
    logic         overflow;

    gowin_ddr3_native #(.ADDR_BITS(ADDR_BITS), .READ_DEPTH(4)) dut (.*);

    // ---- Controller model ----------------------------------------------
    logic [255:0] dram [WORDS];
    logic [31:0]  lfsr = 32'hace1;
    always_ff @(posedge clk)
        lfsr <= {lfsr[30:0], lfsr[31] ^ lfsr[21] ^ lfsr[1] ^ lfsr[0]};
    assign ctrl_cmd_ready   = lfsr[2] | lfsr[9];
    assign ctrl_wr_data_rdy = lfsr[5] | lfsr[17];

    typedef struct { logic [255:0] data; int due; } resp_t;
    resp_t resp_q [$];
    int    cycle = 0, last_due = 0, errors = 0;

    always @(posedge clk) begin
        cycle <= cycle + 1;
        ctrl_rd_data_valid <= 1'b0;
        if (!rst && ctrl_cmd_en) begin
            if (!ctrl_cmd_ready) begin $display("FAIL: cmd_en while not ready"); errors++; end
            if (ctrl_addr[2:0] != 0 || ctrl_addr[28:3] >= WORDS) begin
                $display("FAIL: bad controller address %h", ctrl_addr); errors++;
            end
            if (ctrl_cmd == 3'b000) begin
                if (!ctrl_wr_data_en || !ctrl_wr_data_end || !ctrl_wr_data_rdy) begin
                    $display("FAIL: write command without same-cycle data"); errors++;
                end
                for (int b = 0; b < 32; b++)
                    if (!ctrl_wr_data_mask[b])
                        dram[ctrl_addr[28:3]][8*b +: 8] = ctrl_wr_data[8*b +: 8];
            end else begin
                resp_t r;
                r.data = dram[ctrl_addr[28:3]];
                r.due  = cycle + 3 + int'(lfsr[15:12]);
                if (r.due <= last_due) r.due = last_due + 1;
                last_due = r.due;
                resp_q.push_back(r);
            end
        end
        if (!rst && ctrl_wr_data_en && !(ctrl_cmd_en && ctrl_cmd == 3'b000)) begin
            $display("FAIL: write data without write command"); errors++;
        end
        if (resp_q.size() != 0 && resp_q[0].due <= cycle) begin
            ctrl_rd_data       <= resp_q[0].data;
            ctrl_rd_data_valid <= 1'b1;
            void'(resp_q.pop_front());
        end
    end

    // ---- LiteDRAM-style master -------------------------------------------
    // Clocked so every handshake is judged on the values at the accepting
    // edge.  Write data trails its command by 0-3 cycles, and a further
    // command may be presented before earlier write data has been sent, as
    // skew between the clock-crossing FIFOs allows.
    typedef struct { logic [255:0] data; logic [31:0] mask; int due; } wr_t;
    logic [255:0] model [WORDS];
    logic [255:0] expect_q [$];
    wr_t          wq [$];
    int           issued = 0, checked = 0;

    function automatic logic [255:0] random256();
        logic [255:0] v;
        for (int i = 0; i < 8; i++) v[32*i +: 32] = $urandom;
        return v;
    endfunction

    function automatic logic [31:0] random_mask();
        case ($urandom_range(0, 3))
            0:       return '1;
            1:       return 32'd1 << $urandom_range(0, 31);
            2:       return '0;
            default: return $urandom;
        endcase
    endfunction

    assign rdata_ready = lfsr[23] | lfsr[27] | lfsr[29];

    always @(posedge clk) begin
        if (!rst) begin
            // Command channel.
            if (!cmd_valid || cmd_ready) begin
                cmd_valid <= 1'b0;
                if (issued < OPS && wq.size() < 3 && $urandom_range(0, 3) != 0) begin
                    int addr = $urandom_range(0, WORDS - 1);
                    cmd_valid <= 1'b1;
                    cmd_addr  <= ADDR_BITS'(addr);
                    issued++;
                    if ($urandom_range(0, 1)) begin
                        wr_t w;
                        w.data = random256();
                        w.mask = random_mask();
                        w.due  = cycle + $urandom_range(0, 3);
                        for (int b = 0; b < 32; b++)
                            if (w.mask[b]) model[addr][8*b +: 8] = w.data[8*b +: 8];
                        wq.push_back(w);
                        cmd_we <= 1'b1;
                    end else begin
                        expect_q.push_back(model[addr]);
                        cmd_we <= 1'b0;
                    end
                end
            end

            // Write-data channel.
            if (!wdata_valid || wdata_ready) begin
                wdata_valid <= 1'b0;
                if (wq.size() != 0 && wq[0].due <= cycle) begin
                    wdata_valid <= 1'b1;
                    wdata_data  <= wq[0].data;
                    wdata_we    <= wq[0].mask;
                    void'(wq.pop_front());
                end
            end

            // Read-data channel.
            if (rdata_valid && rdata_ready) begin
                if (expect_q.size() == 0) begin
                    $display("FAIL: unexpected read data"); errors++;
                end else begin
                    if (rdata_data !== expect_q[0]) begin
                        $display("FAIL: read mismatch at check %0d", checked); errors++;
                    end
                    void'(expect_q.pop_front());
                    checked++;
                end
            end
        end
    end

    initial begin
        for (int i = 0; i < WORDS; i++) begin
            dram[i]  = '0;
            model[i] = '0;
        end
        repeat (5) @(posedge clk);
        rst <= 1'b0;

        wait (issued == OPS);
        repeat (300) @(posedge clk);
        if (expect_q.size() != 0 || wq.size() != 0 || wdata_valid || cmd_valid) begin
            $display("FAIL: %0d reads and %0d writes never completed", expect_q.size(), wq.size());
            errors++;
        end
        if (overflow) begin $display("FAIL: return FIFO overflow"); errors++; end
        if (reads != checked || reads + writes != OPS) begin
            $display("FAIL: issued %0d reads + %0d writes, checked %0d", reads, writes, checked); errors++;
        end

        if (errors == 0)
            $display("PASS: %0d commands (%0d writes, %0d reads checked)", OPS, writes, checked);
        else
            $display("FAIL: %0d errors", errors);
        $finish;
    end

    initial begin
        #20ms;
        $display("FAIL: timeout (issued %0d, checked %0d)", issued, checked);
        $finish;
    end
endmodule
