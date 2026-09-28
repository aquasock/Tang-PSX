// SPDX-License-Identifier: GPL-3.0-only
//
// Standalone proof of the Gowin DDR3 controller on the Tang Console 138K.
//
// The x32 controller configuration, its 400 MHz PLL, and the PG484 pin mapping
// follow Sipeed's TangMega-138K-example ddr_memory design (Apache-2.0, commit
// 06e7d8b), regenerated locally with Gowin EDA; see scripts/gen-ddr3-ip.sh.
// Instead of Sipeed's LED/text result, a full-array tester reports detailed
// status through the Tang-Control FPGA transport.  The transport runs from the
// 50 MHz board clock so it remains readable if the DDR PLL or calibration
// fails.

module tang_psx_ddr3_top (
    input  logic        clk,            // 50 MHz board oscillator
    input  logic        uart_rx,
    output logic        uart_tx,

    output logic [14:0] ddr_addr,
    output logic [2:0]  ddr_bank,
    output logic        ddr_cs,
    output logic        ddr_ras,
    output logic        ddr_cas,
    output logic        ddr_we,
    output logic        ddr_ck,
    output logic        ddr_ck_n,
    output logic        ddr_cke,
    output logic        ddr_odt,
    output logic        ddr_reset_n,
    output logic [3:0]  ddr_dm,
    inout  wire  [31:0] ddr_dq,
    inout  wire  [3:0]  ddr_dqs,
    inout  wire  [3:0]  ddr_dqs_n
);

    localparam logic [31:0] DIAG_MAGIC = 32'h54504433;  // "TPD3"
    localparam logic [31:0] DIAG_ABI   = 32'h00010000;

    // ------------------------------------------------------------------
    // Board-clock power-on reset (~1.3 ms).
    // ------------------------------------------------------------------
    logic [15:0] por_count = '1;
    logic        por_n = 1'b0;

    always_ff @(posedge clk) begin
        if (por_count != 0)
            por_count <= por_count - 16'd1;
        por_n <= (por_count == 0);
    end

    // ------------------------------------------------------------------
    // DDR clocking: 50 MHz -> 800 MHz VCO -> 400 MHz memory clock.
    // PLL_INIT performs Gowin's charge-pump/loop-filter search on the
    // dynamically configured PLL, exactly as the vendor reference does.
    // ------------------------------------------------------------------
    logic       memory_clk;
    logic       pll_lock;
    logic       pll_stop;
    logic       pll_raw_lock;
    logic       pll_rst;
    logic [5:0] pll_icpsel;
    logic [2:0] pll_lpfres;

    Gowin_PLL_MOD u_pll (
        .clkin   (clk),
        .reset   (pll_rst),
        .icpsel  (pll_icpsel),
        .lpfres  (pll_lpfres),
        .lpfcap  (2'b00),
        .enclk0  (1'b1),
        .enclk1  (1'b1),
        .enclk2  (pll_stop),
        .clkout0 (),
        .clkout1 (),
        .clkout2 (memory_clk),
        .lock    (pll_raw_lock)
    );

    PLL_INIT #(
        .CLK_PERIOD (20),
        .MULTI_FAC  (16)
    ) u_pll_init (
        .CLKIN   (clk),
        .I_RST   (!por_n),
        .O_RST   (pll_rst),
        .PLLLOCK (pll_raw_lock),
        .O_LOCK  (pll_lock),
        .ICPSEL  (pll_icpsel),
        .LPFRES  (pll_lpfres)
    );

    // ------------------------------------------------------------------
    // Gowin DDR3 controller (x32, 1:4, 100 MHz user clock).
    // ------------------------------------------------------------------
    logic         ui_clk;
    logic         ddr_rst;
    logic         calib_done;
    logic         cmd_ready;
    logic [2:0]   cmd;
    logic         cmd_en;
    logic [28:0]  addr;
    logic         wr_data_rdy;
    logic [255:0] wr_data;
    logic         wr_data_en;
    logic         wr_data_end;
    logic [31:0]  wr_data_mask;
    logic [255:0] rd_data;
    logic         rd_data_valid;
    logic         rd_data_end;

    DDR3_Memory_Interface_Top u_ddr3 (
        .clk                 (clk),
        .memory_clk          (memory_clk),
        .pll_lock            (pll_lock),
        .pll_stop            (pll_stop),
        .rst_n               (por_n),
        .clk_out             (ui_clk),
        .ddr_rst             (ddr_rst),
        .init_calib_complete (calib_done),
        .cmd_ready           (cmd_ready),
        .cmd                 (cmd),
        .cmd_en              (cmd_en),
        .addr                (addr),
        .wr_data_rdy         (wr_data_rdy),
        .wr_data             (wr_data),
        .wr_data_en          (wr_data_en),
        .wr_data_end         (wr_data_end),
        .wr_data_mask        (wr_data_mask),
        .rd_data             (rd_data),
        .rd_data_valid       (rd_data_valid),
        .rd_data_end         (rd_data_end),
        .sr_req              (1'b0),
        .ref_req             (1'b0),
        .sr_ack              (),
        .ref_ack             (),
        .burst               (1'b0),
        .O_ddr_addr          (ddr_addr),
        .O_ddr_ba            (ddr_bank),
        .O_ddr_cs_n          (ddr_cs),
        .O_ddr_ras_n         (ddr_ras),
        .O_ddr_cas_n         (ddr_cas),
        .O_ddr_we_n          (ddr_we),
        .O_ddr_clk           (ddr_ck),
        .O_ddr_clk_n         (ddr_ck_n),
        .O_ddr_cke           (ddr_cke),
        .O_ddr_odt           (ddr_odt),
        .O_ddr_reset_n       (ddr_reset_n),
        .O_ddr_dqm           (ddr_dm),
        .IO_ddr_dq           (ddr_dq),
        .IO_ddr_dqs          (ddr_dqs),
        .IO_ddr_dqs_n        (ddr_dqs_n)
    );

    // Controller reset is asserted asynchronously and released on ui_clk.
    logic [1:0] ui_rst_sync = 2'b11;
    always_ff @(posedge ui_clk or posedge ddr_rst) begin
        if (ddr_rst)
            ui_rst_sync <= 2'b11;
        else
            ui_rst_sync <= {ui_rst_sync[0], 1'b0};
    end

    logic [1:0] calib_sync_ui = 2'b00;
    always_ff @(posedge ui_clk)
        calib_sync_ui <= {calib_sync_ui[0], calib_done};

    // ------------------------------------------------------------------
    // Memory test.
    // ------------------------------------------------------------------
    localparam int SNAP_WORDS = 31;

    logic [31:0] t_flags, t_pass_count, t_error_passes, t_progress, t_error_bursts;
    logic [31:0] t_error_mask, t_first_burst, t_first_pass, t_first_mask, t_last_burst;
    logic [31:0] t_write_cycles, t_read_cycles, t_verified, t_stalls, t_heartbeat;
    logic [255:0] t_first_expected, t_first_observed;

    ddr3_tester u_tester (
        .clk                  (ui_clk),
        .rst                  (ui_rst_sync[1]),
        .calib_done           (calib_sync_ui[1]),
        .cmd_ready            (cmd_ready),
        .cmd                  (cmd),
        .cmd_en               (cmd_en),
        .addr                 (addr),
        .wr_data_rdy          (wr_data_rdy),
        .wr_data              (wr_data),
        .wr_data_en           (wr_data_en),
        .wr_data_end          (wr_data_end),
        .wr_data_mask         (wr_data_mask),
        .rd_data              (rd_data),
        .rd_data_valid        (rd_data_valid),
        .flags                (t_flags),
        .pass_count           (t_pass_count),
        .error_passes         (t_error_passes),
        .progress             (t_progress),
        .error_bursts         (t_error_bursts),
        .error_mask           (t_error_mask),
        .first_error_burst    (t_first_burst),
        .first_error_pass     (t_first_pass),
        .first_error_mask     (t_first_mask),
        .last_error_burst     (t_last_burst),
        .write_cycles         (t_write_cycles),
        .read_cycles          (t_read_cycles),
        .verified_bursts      (t_verified),
        .stall_events         (t_stalls),
        .heartbeat            (t_heartbeat),
        .first_error_expected (t_first_expected),
        .first_error_observed (t_first_observed)
    );

    logic [32*SNAP_WORDS-1:0] snap_src, snap;
    logic [31:0]              snap_count;

    assign snap_src = {t_first_observed, t_first_expected,
                       t_heartbeat, t_stalls, t_verified, t_read_cycles, t_write_cycles,
                       t_last_burst, t_first_mask, t_first_pass, t_first_burst,
                       t_error_mask, t_error_bursts, t_progress, t_error_passes,
                       t_pass_count, t_flags};

    status_snapshot #(.WIDTH(32*SNAP_WORDS)) u_snapshot (
        .src_clk  (ui_clk),
        .src_data (snap_src),
        .dst_clk  (clk),
        .dst_rst  (!por_n),
        .dst_data (snap),
        .count    (snap_count)
    );

    // ------------------------------------------------------------------
    // Board-clock status.
    // ------------------------------------------------------------------
    logic [1:0]  lock_sync = 2'b00;
    logic [1:0]  calib_sync = 2'b00;
    logic [1:0]  ddr_rst_sync = 2'b00;
    logic [31:0] uptime;
    logic [31:0] calib_time;
    logic        calib_seen;

    always_ff @(posedge clk) begin
        lock_sync    <= {lock_sync[0], pll_lock};
        calib_sync   <= {calib_sync[0], calib_done};
        ddr_rst_sync <= {ddr_rst_sync[0], ddr_rst};
        uptime       <= uptime + 32'd1;
        if (!calib_seen) begin
            calib_time <= calib_time + 32'd1;
            calib_seen <= calib_sync[1];
        end
        if (!por_n) begin
            uptime     <= '0;
            calib_time <= '0;
            calib_seen <= 1'b0;
        end
    end

    wire [31:0] live_flags = {27'd0, calib_seen, ddr_rst_sync[1], calib_sync[1],
                              lock_sync[1], por_n};

    // ------------------------------------------------------------------
    // Tang-Control transport and register file.
    // ------------------------------------------------------------------
    logic        debug_valid;
    logic        debug_write;
    logic [31:0] debug_address;
    logic [31:0] debug_wdata;
    logic [31:0] debug_rdata;
    logic [31:0] crc_errors;
    logic [31:0] bad_requests;

    function automatic logic [31:0] snap_word(input int i);
        return snap[32*i +: 32];
    endfunction

    // The read address is stable several UART bytes before the response is
    // sampled, so a registered mux adds no protocol risk.
    always_ff @(posedge clk) begin
        unique case (debug_address[7:2])
            6'h00:   debug_rdata <= DIAG_MAGIC;
            6'h01:   debug_rdata <= DIAG_ABI;
            6'h02:   debug_rdata <= live_flags;
            6'h03:   debug_rdata <= snap_word(0);   // tester flags
            6'h04:   debug_rdata <= snap_word(1);   // completed passes
            6'h05:   debug_rdata <= snap_word(2);   // passes with errors
            6'h06:   debug_rdata <= snap_word(3);   // bursts in current phase
            6'h07:   debug_rdata <= snap_word(4);   // bursts with errors
            6'h08:   debug_rdata <= snap_word(5);   // cumulative byte mask
            6'h09:   debug_rdata <= snap_word(6);   // first error burst
            6'h0a:   debug_rdata <= snap_word(7);   // first error pass
            6'h0b:   debug_rdata <= snap_word(8);   // first error byte mask
            6'h0c:   debug_rdata <= snap_word(9);   // last error burst
            6'h0d:   debug_rdata <= snap_word(10);  // last write sweep cycles
            6'h0e:   debug_rdata <= snap_word(11);  // last read sweep cycles
            6'h0f:   debug_rdata <= snap_word(12);  // verified bursts
            6'h10:   debug_rdata <= snap_word(13);  // read stall events
            6'h11:   debug_rdata <= snap_word(14);  // ui_clk heartbeat
            6'h12:   debug_rdata <= snap_count;
            6'h13:   debug_rdata <= calib_time;
            6'h14:   debug_rdata <= uptime;
            6'h15:   debug_rdata <= crc_errors;
            6'h16:   debug_rdata <= bad_requests;
            6'h20, 6'h21, 6'h22, 6'h23, 6'h24, 6'h25, 6'h26, 6'h27:
                     debug_rdata <= snap_word(15 + int'(debug_address[4:2]));  // expected
            6'h28, 6'h29, 6'h2a, 6'h2b, 6'h2c, 6'h2d, 6'h2e, 6'h2f:
                     debug_rdata <= snap_word(23 + int'(debug_address[4:2]));  // observed
            default: debug_rdata <= 32'd0;
        endcase
    end

    iosys_bl616 #(
        .CORE_ID (16'h0051),
        .FREQ    (50_000_000)
    ) u_iosys (
        .clk                (clk),
        .hclk               (clk),
        .resetn             (por_n),
        .overlay            (),
        .overlay_x          (8'd0),
        .overlay_y          (8'd0),
        .overlay_color      (),
        .joy1               (12'd0),
        .joy2               (12'd0),
        .hid1               (),
        .hid2               (),
        .rom_loading        (),
        .rom_do             (),
        .rom_do_valid       (),
        .mgmt_address       (),
        .mgmt_read          (),
        .mgmt_readdata      (16'd0),
        .mgmt_write         (),
        .mgmt_writedata     (),
        .fdd_request        (2'd0),
        .kbd_data           (),
        .kbd_data_valid     (),
        .core_config        (),
        .debug_valid        (debug_valid),
        .debug_write        (debug_write),
        .debug_address      (debug_address),
        .debug_wdata        (debug_wdata),
        .debug_rdata        (debug_rdata),
        .debug_crc_errors   (crc_errors),
        .debug_bad_requests (bad_requests),
        .stream_start       (),
        .stream_end         (),
        .stream_cancel      (),
        .stream_id          (),
        .stream_offset      (),
        .stream_data        (),
        .stream_valid       (),
        .stream_ready       (1'b1),
        .uart_rx            (uart_rx),
        .uart_tx            (uart_tx)
    );

endmodule
