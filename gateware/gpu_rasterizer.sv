// SPDX-License-Identifier: GPL-3.0-only
//
// Gate 1 PSX primitive rasterizer.  The AE350 performs packet decoding and
// exact divide-based triangle setup, then sends the quotient/remainder edge
// walkers used by the portable reference renderer.  This block owns every
// per-pixel step and accesses the 1024x512 BGR555 VRAM through a 256-bit DDR
// native port.

module gpu_rasterizer #(
    parameter int ADDR_BITS = 25,
    parameter logic [ADDR_BITS-1:0] VRAM_WORD_BASE = 'h1ff0000
) (
    input  logic                 clk,
    input  logic                 rst,

    input  logic                 cmd_valid,
    output logic                 cmd_ready,
    input  logic [31:0]          cmd_data,

    output logic                 idle,
    output logic                 error,
    output logic [31:0]          primitives,
    output logic [31:0]          pixels,

    output logic                 mem_cmd_valid,
    input  logic                 mem_cmd_ready,
    output logic                 mem_cmd_we,
    output logic [ADDR_BITS-1:0] mem_cmd_addr,
    output logic                 mem_wdata_valid,
    input  logic                 mem_wdata_ready,
    output logic [255:0]         mem_wdata_data,
    output logic [31:0]          mem_wdata_we,
    input  logic                 mem_rdata_valid,
    output logic                 mem_rdata_ready,
    input  logic [255:0]         mem_rdata_data
);

    localparam logic [31:0] OP_TRIANGLE  = 32'h47500001;
    localparam logic [31:0] OP_RECTANGLE = 32'h47500002;

    localparam logic [5:0]
        S_IDLE          = 0,
        S_LOAD          = 1,
        S_TRI_INIT      = 2,
        S_RECT_INIT     = 3,
        S_PIXEL         = 4,
        S_LOOKUP        = 5,
        S_READ_CMD      = 6,
        S_READ_WAIT     = 7,
        S_TEX_PACKED    = 8,
        S_TEX_CLUT      = 9,
        S_TEX_READY     = 10,
        S_MASK_READY    = 11,
        S_PIXEL_COMMIT  = 12,
        S_FLUSH_CMD     = 13,
        S_FLUSH_DATA    = 14,
        S_FINISH        = 15,
        S_DONE          = 16,
        S_TEX_MULT      = 17,
        S_TEX_SHADE     = 18;

    logic [5:0] state;
    logic [5:0] resume_state;
    logic [5:0] after_flush;
    logic [47:1] triangle_load;
    logic [12:1] rectangle_load;
    logic       triangle_mode;
    logic       row_started;

    logic [31:0] flags;
    logic signed [31:0] min_x, max_x, min_y, max_y;
    logic signed [31:0] area;
    logic signed [31:0] row_edge [0:2];
    logic signed [31:0] edge_now [0:2];
    logic signed [31:0] edge_dx [0:2];
    logic signed [31:0] edge_dy [0:2];
    logic signed [31:0] attr_row_q [0:4];
    logic signed [31:0] attr_row_r [0:4];
    logic signed [31:0] attr_q [0:4];
    logic signed [31:0] attr_r [0:4];
    logic signed [31:0] attr_qx [0:4];
    logic signed [31:0] attr_rx [0:4];
    logic signed [31:0] attr_qy [0:4];
    logic signed [31:0] attr_ry [0:4];
    // Each attribute is q + r/area with r in [0, area), and so are its steps
    // (qx, rx) and (qy, ry). A step's remainder carries into q exactly when
    // r + (rx - area) >= 0, so with rx - area and ry - area formed once per
    // triangle, the next remainder and quotient are each one add and a
    // select, and a pixel step takes a single clock. Vertex coordinates are
    // 11-bit signed plus an 11-bit signed offset, so |x|, |y| <= 2048 and
    // area < 2^25; remainders and r + (rx - area) therefore fit 27 bits,
    // which shortens the carry chain. Synthesis must keep the per-triangle
    // constants in flip-flops: as LUT RAM they start the step's longest path.
    logic signed [31:0] attr_rx_ma [0:4] /* synthesis syn_ramstyle = "registers" */;
    logic signed [31:0] attr_ry_ma [0:4] /* synthesis syn_ramstyle = "registers" */;
    logic signed [26:0] over_x [0:4];
    logic signed [26:0] over_y [0:4];
    logic signed [31:0] next_x_q [0:4];
    logic signed [31:0] next_x_r [0:4];
    logic signed [31:0] next_y_q [0:4];
    logic signed [31:0] next_y_r [0:4];
    logic [31:0] clut, page;
    logic signed [31:0] current_x, current_y;
    // Registered current_x == max_x and current_y == max_y, updated with
    // every triangle-mode position change, so a step's choice between the
    // next pixel and the next row does not depend on two 32-bit compares.
    logic x_last, y_last;

    logic signed [31:0] rect_x0, rect_y0;
    logic [10:0] rect_width, rect_col;
    logic [9:0] rect_height, rect_row;
    logic signed [31:0] rect_r, rect_g, rect_b;
    logic [7:0] rect_u0, rect_v0, rect_u, rect_v;

    logic [7:0] tex_u;
    logic [1:0] tex_depth;
    logic [15:0] texel;
    logic [15:0] mod_texel;
    logic signed [31:0] mod_red, mod_green, mod_blue;
    logic signed [31:0] mod_product_red, mod_product_green,
        mod_product_blue;

    logic [ADDR_BITS-1:0] lookup_addr;
    logic [3:0] lookup_lane;
    logic [15:0] lookup_value;
    logic lookup_hit;
    logic [15:0] lookup_hit_value;

    logic cache0_valid, cache1_valid, cache_replace;
    logic [ADDR_BITS-1:0] cache0_addr, cache1_addr;
    logic [255:0] cache0_data, cache1_data;

    logic out_valid;
    logic [ADDR_BITS-1:0] out_addr;
    logic [255:0] out_data;
    logic [31:0] out_we;
    logic [18:0] destination_pixel;
    logic [15:0] pending_pixel;

    wire textured = flags[0];
    wire raw_texture = flags[1];
    wire mask_test = flags[2];
    wire mask_set = flags[3];
    wire triangle_inside = ~(edge_now[0][31] | edge_now[1][31] |
        edge_now[2][31]);

    wire [7:0] active_u = triangle_mode ? attr_q[3][7:0] : rect_u;
    wire [7:0] active_v = triangle_mode ? attr_q[4][7:0] : rect_v;
    wire [9:0] texture_page_x = {page[3:0], 6'b0};
    wire [8:0] texture_page_y = {page[4], 8'b0};
    wire [9:0] texture_x = page[8:7] == 0 ?
        (texture_page_x + {4'b0, active_u[7:2]}) :
        (page[8:7] == 1 ? (texture_page_x + {3'b0, active_u[7:1]}) :
                          (texture_page_x + {2'b0, active_u}));
    wire [8:0] texture_y = texture_page_y + {1'b0, active_v};
    wire [19:0] texture_pixel_index = {texture_y[8:0], 10'b0} +
        {10'b0, texture_x[9:0]};
    wire [18:0] current_pixel_index = {current_y[8:0], 10'b0} +
        {9'b0, current_x[9:0]};

    function automatic logic [15:0] line_pixel(
        input logic [255:0] line, input logic [3:0] lane);
        case (lane)
            0:  line_pixel = line[15:0];
            1:  line_pixel = line[31:16];
            2:  line_pixel = line[47:32];
            3:  line_pixel = line[63:48];
            4:  line_pixel = line[79:64];
            5:  line_pixel = line[95:80];
            6:  line_pixel = line[111:96];
            7:  line_pixel = line[127:112];
            8:  line_pixel = line[143:128];
            9:  line_pixel = line[159:144];
            10: line_pixel = line[175:160];
            11: line_pixel = line[191:176];
            12: line_pixel = line[207:192];
            13: line_pixel = line[223:208];
            14: line_pixel = line[239:224];
            default: line_pixel = line[255:240];
        endcase
    endfunction

    function automatic logic [15:0] shade15(
        input logic signed [31:0] red,
        input logic signed [31:0] green,
        input logic signed [31:0] blue);
        logic [7:0] r, g, b;
        begin
            r = red < 0 ? 0 : (red > 255 ? 255 : red[7:0]);
            g = green < 0 ? 0 : (green > 255 ? 255 : green[7:0]);
            b = blue < 0 ? 0 : (blue > 255 ? 255 : blue[7:0]);
            shade15 = {1'b0, b[7:3], g[7:3], r[7:3]};
        end
    endfunction

    // Move to the first pixel of the next row, or finish the triangle.
    task automatic next_row;
        begin
            if (y_last) begin
                state <= S_FINISH;
            end else begin
                current_x <= min_x;
                current_y <= current_y + 1;
                x_last <= min_x == max_x;
                y_last <= current_y + 1 == max_y;
                row_started <= 1'b0;
                for (int k = 0; k < 3; k = k + 1) begin
                    row_edge[k] <= row_edge[k] + edge_dy[k];
                    edge_now[k] <= row_edge[k] + edge_dy[k];
                end
                for (int k = 0; k < 5; k = k + 1) begin
                    attr_row_q[k] <= next_y_q[k];
                    attr_row_r[k] <= next_y_r[k];
                    attr_q[k] <= next_y_q[k];
                    attr_r[k] <= next_y_r[k];
                end
                state <= S_PIXEL;
            end
        end
    endtask

    // Finish the current pixel and move to the next one in the same clock.
    task automatic advance_pixel;
        begin
            if (triangle_mode) begin
                if (x_last) begin
                    next_row();
                end else begin
                    current_x <= current_x + 1;
                    x_last <= current_x + 1 == max_x;
                    for (int k = 0; k < 3; k = k + 1)
                        edge_now[k] <= edge_now[k] + edge_dx[k];
                    for (int k = 0; k < 5; k = k + 1) begin
                        attr_q[k] <= next_x_q[k];
                        attr_r[k] <= next_x_r[k];
                    end
                    state <= S_PIXEL;
                end
            end else if (rect_col + 1'b1 >= rect_width) begin
                if (rect_row + 1'b1 >= rect_height) begin
                    state <= S_FINISH;
                end else begin
                    rect_col <= 0;
                    rect_row <= rect_row + 1'b1;
                    current_x <= rect_x0;
                    current_y <= current_y + 1;
                    rect_u <= rect_u0;
                    rect_v <= rect_v + 1'b1;
                    state <= S_PIXEL;
                end
            end else begin
                rect_col <= rect_col + 1'b1;
                current_x <= current_x + 1;
                rect_u <= rect_u + 1'b1;
                state <= S_PIXEL;
            end
        end
    endtask

    always_comb begin
        for (int k = 0; k < 5; k = k + 1) begin
            over_x[k] = attr_r[k][26:0] + attr_rx_ma[k][26:0];
            over_y[k] = attr_row_r[k][26:0] + attr_ry_ma[k][26:0];
            next_x_r[k] = over_x[k][26] ?
                32'(attr_r[k][26:0] + attr_rx[k][26:0]) : 32'(over_x[k]);
            next_x_q[k] = over_x[k][26] ? attr_q[k] + attr_qx[k] :
                attr_q[k] + attr_qx[k] + 32'sd1;
            next_y_r[k] = over_y[k][26] ?
                32'(attr_row_r[k][26:0] + attr_ry[k][26:0]) : 32'(over_y[k]);
            next_y_q[k] = over_y[k][26] ? attr_row_q[k] + attr_qy[k] :
                attr_row_q[k] + attr_qy[k] + 32'sd1;
        end
    end

    always_comb begin
        cmd_ready = state == S_IDLE || state == S_LOAD;
        idle = state == S_IDLE;
        mem_cmd_valid = 1'b0;
        mem_cmd_we = 1'b0;
        mem_cmd_addr = '0;
        mem_wdata_valid = 1'b0;
        mem_wdata_data = out_data;
        mem_wdata_we = out_we;
        // One read is in flight at a time and its data returns only in
        // S_READ_WAIT, so a constant ready keeps the state decode out of the
        // DDR read-return path.
        mem_rdata_ready = 1'b1;
        if (state == S_READ_CMD) begin
            mem_cmd_valid = 1'b1;
            mem_cmd_addr = lookup_addr;
        end else if (state == S_FLUSH_CMD) begin
            mem_cmd_valid = 1'b1;
            mem_cmd_we = 1'b1;
            mem_cmd_addr = out_addr;
        end else if (state == S_FLUSH_DATA) begin
            mem_wdata_valid = 1'b1;
        end

        lookup_hit = 1'b0;
        lookup_hit_value = 16'b0;
        if (out_valid && out_addr == lookup_addr &&
            out_we[{lookup_lane, 1'b0} +: 2] == 2'b11) begin
            lookup_hit = 1'b1;
            lookup_hit_value = line_pixel(out_data, lookup_lane);
        end else if (cache0_valid && cache0_addr == lookup_addr) begin
            lookup_hit = 1'b1;
            lookup_hit_value = line_pixel(cache0_data, lookup_lane);
        end else if (cache1_valid && cache1_addr == lookup_addr) begin
            lookup_hit = 1'b1;
            lookup_hit_value = line_pixel(cache1_data, lookup_lane);
        end
    end

    integer n;
    // Reset is applied after the body so that it overrides only the control
    // registers.  Datapath registers carry no reset term, which keeps the
    // reset pulse out of their clock enables; every field they hold is
    // reloaded by the next descriptor before it is used.
    always_ff @(posedge clk) begin
        // Descriptor fields are selected by shifting one-hot strobes.
        // This avoids a wide binary-index decoder on every destination
        // register clock-enable.
        if (cmd_valid) begin
            if (triangle_load[1])  flags <= cmd_data;
            if (triangle_load[2])  min_x <= $signed(cmd_data);
            if (triangle_load[3])  max_x <= $signed(cmd_data);
            if (triangle_load[4])  min_y <= $signed(cmd_data);
            if (triangle_load[5])  max_y <= $signed(cmd_data);
            if (triangle_load[6])  area <= $signed(cmd_data);
            if (triangle_load[7])  row_edge[0] <= $signed(cmd_data);
            if (triangle_load[8])  row_edge[1] <= $signed(cmd_data);
            if (triangle_load[9])  row_edge[2] <= $signed(cmd_data);
            if (triangle_load[10]) edge_dx[0] <= $signed(cmd_data);
            if (triangle_load[11]) edge_dx[1] <= $signed(cmd_data);
            if (triangle_load[12]) edge_dx[2] <= $signed(cmd_data);
            if (triangle_load[13]) edge_dy[0] <= $signed(cmd_data);
            if (triangle_load[14]) edge_dy[1] <= $signed(cmd_data);
            if (triangle_load[15]) edge_dy[2] <= $signed(cmd_data);
            if (triangle_load[16]) clut <= cmd_data;
            if (triangle_load[17]) page <= cmd_data;
            if (triangle_load[18]) attr_row_q[0] <= $signed(cmd_data);
            if (triangle_load[19]) attr_row_r[0] <= $signed(cmd_data);
            if (triangle_load[20]) attr_qx[0] <= $signed(cmd_data);
            if (triangle_load[21]) attr_rx[0] <= $signed(cmd_data);
            if (triangle_load[22]) attr_qy[0] <= $signed(cmd_data);
            if (triangle_load[23]) attr_ry[0] <= $signed(cmd_data);
            if (triangle_load[24]) attr_row_q[1] <= $signed(cmd_data);
            if (triangle_load[25]) attr_row_r[1] <= $signed(cmd_data);
            if (triangle_load[26]) attr_qx[1] <= $signed(cmd_data);
            if (triangle_load[27]) attr_rx[1] <= $signed(cmd_data);
            if (triangle_load[28]) attr_qy[1] <= $signed(cmd_data);
            if (triangle_load[29]) attr_ry[1] <= $signed(cmd_data);
            if (triangle_load[30]) attr_row_q[2] <= $signed(cmd_data);
            if (triangle_load[31]) attr_row_r[2] <= $signed(cmd_data);
            if (triangle_load[32]) attr_qx[2] <= $signed(cmd_data);
            if (triangle_load[33]) attr_rx[2] <= $signed(cmd_data);
            if (triangle_load[34]) attr_qy[2] <= $signed(cmd_data);
            if (triangle_load[35]) attr_ry[2] <= $signed(cmd_data);
            if (triangle_load[36]) attr_row_q[3] <= $signed(cmd_data);
            if (triangle_load[37]) attr_row_r[3] <= $signed(cmd_data);
            if (triangle_load[38]) attr_qx[3] <= $signed(cmd_data);
            if (triangle_load[39]) attr_rx[3] <= $signed(cmd_data);
            if (triangle_load[40]) attr_qy[3] <= $signed(cmd_data);
            if (triangle_load[41]) attr_ry[3] <= $signed(cmd_data);
            if (triangle_load[42]) attr_row_q[4] <= $signed(cmd_data);
            if (triangle_load[43]) attr_row_r[4] <= $signed(cmd_data);
            if (triangle_load[44]) attr_qx[4] <= $signed(cmd_data);
            if (triangle_load[45]) attr_rx[4] <= $signed(cmd_data);
            if (triangle_load[46]) attr_qy[4] <= $signed(cmd_data);
            if (triangle_load[47]) attr_ry[4] <= $signed(cmd_data);

            if (rectangle_load[1])  flags <= cmd_data;
            if (rectangle_load[2])  rect_x0 <= $signed(cmd_data);
            if (rectangle_load[3])  rect_y0 <= $signed(cmd_data);
            if (rectangle_load[4])  rect_width <= cmd_data[10:0];
            if (rectangle_load[5])  rect_height <= cmd_data[9:0];
            if (rectangle_load[6])  rect_r <= $signed(cmd_data);
            if (rectangle_load[7])  rect_g <= $signed(cmd_data);
            if (rectangle_load[8])  rect_b <= $signed(cmd_data);
            if (rectangle_load[9])  rect_u0 <= cmd_data[7:0];
            if (rectangle_load[10]) rect_v0 <= cmd_data[7:0];
            if (rectangle_load[11]) clut <= cmd_data;
            if (rectangle_load[12]) page <= cmd_data;
        end

        case (state)
            S_IDLE: begin
                if (cmd_valid) begin
                    if (cmd_data == OP_TRIANGLE) begin
                        triangle_mode <= 1'b1;
                        triangle_load <= {{46{1'b0}}, 1'b1};
                        rectangle_load <= '0;
                        state <= S_LOAD;
                    end else if (cmd_data == OP_RECTANGLE) begin
                        triangle_mode <= 1'b0;
                        triangle_load <= '0;
                        rectangle_load <= {{11{1'b0}}, 1'b1};
                        state <= S_LOAD;
                    end else begin
                        error <= 1'b1;
                    end
                end
            end

            S_LOAD: if (cmd_valid) begin
                if (triangle_mode) begin
                    if (triangle_load[47]) begin
                        triangle_load <= '0;
                        state <= S_TRI_INIT;
                    end else begin
                        triangle_load <= triangle_load << 1;
                    end
                end else begin
                    if (rectangle_load[12]) begin
                        rectangle_load <= '0;
                        state <= S_RECT_INIT;
                    end else begin
                        rectangle_load <= rectangle_load << 1;
                    end
                end
            end

            S_TRI_INIT: begin
                current_x <= min_x;
                current_y <= min_y;
                x_last <= min_x == max_x;
                y_last <= min_y == max_y;
                row_started <= 1'b0;
                for (n = 0; n < 3; n = n + 1)
                    edge_now[n] <= row_edge[n];
                for (n = 0; n < 5; n = n + 1) begin
                    attr_q[n] <= attr_row_q[n];
                    attr_r[n] <= attr_row_r[n];
                    attr_rx_ma[n] <= attr_rx[n] - area;
                    attr_ry_ma[n] <= attr_ry[n] - area;
                end
                state <= S_PIXEL;
            end

            S_RECT_INIT: begin
                rect_col <= 0;
                rect_row <= 0;
                current_x <= rect_x0;
                current_y <= rect_y0;
                rect_u <= rect_u0;
                rect_v <= rect_v0;
                if (rect_width == 0 || rect_height == 0)
                    state <= S_FINISH;
                else
                    state <= S_PIXEL;
            end

            S_PIXEL: begin
                if (triangle_mode && !triangle_inside) begin
                    if (row_started) begin
                        next_row();
                    end else begin
                        advance_pixel();
                    end
                end else if (textured) begin
                    if (triangle_mode)
                        row_started <= 1'b1;
                    tex_u <= active_u;
                    tex_depth <= page[8:7];
                    lookup_addr <= VRAM_WORD_BASE +
                        {{(ADDR_BITS-16){1'b0}}, texture_pixel_index[19:4]};
                    lookup_lane <= texture_pixel_index[3:0];
                    resume_state <= S_TEX_PACKED;
                    state <= S_LOOKUP;
                end else begin
                    if (triangle_mode)
                        row_started <= 1'b1;
                    pending_pixel <= shade15(
                        triangle_mode ? attr_q[0] : rect_r,
                        triangle_mode ? attr_q[1] : rect_g,
                        triangle_mode ? attr_q[2] : rect_b) |
                        (mask_set ? 16'h8000 : 16'h0000);
                    destination_pixel <= current_pixel_index;
                    if (mask_test) begin
                        lookup_addr <= VRAM_WORD_BASE +
                            {{(ADDR_BITS-15){1'b0}},
                            current_pixel_index[18:4]};
                        lookup_lane <= current_x[3:0];
                        resume_state <= S_MASK_READY;
                        state <= S_LOOKUP;
                    end else begin
                        state <= S_PIXEL_COMMIT;
                    end
                end
            end

            S_LOOKUP: begin
                if (lookup_hit) begin
                    lookup_value <= lookup_hit_value;
                    state <= resume_state;
                end else begin
                    state <= S_READ_CMD;
                end
            end

            S_READ_CMD: if (mem_cmd_ready)
                state <= S_READ_WAIT;

            S_READ_WAIT: if (mem_rdata_valid) begin
                lookup_value <= line_pixel(mem_rdata_data, lookup_lane);
                if (cache_replace) begin
                    cache1_valid <= 1'b1;
                    cache1_addr <= lookup_addr;
                    cache1_data <= mem_rdata_data;
                end else begin
                    cache0_valid <= 1'b1;
                    cache0_addr <= lookup_addr;
                    cache0_data <= mem_rdata_data;
                end
                cache_replace <= ~cache_replace;
                state <= resume_state;
            end

            S_TEX_PACKED: begin
                if (tex_depth[1]) begin
                    texel <= lookup_value;
                    state <= S_TEX_READY;
                end else begin
                    logic [7:0] index;
                    logic [10:0] clut_x;
                    logic [18:0] clut_pixel;
                    if (tex_depth == 0) begin
                        case (tex_u[1:0])
                            0: index = {4'b0, lookup_value[3:0]};
                            1: index = {4'b0, lookup_value[7:4]};
                            2: index = {4'b0, lookup_value[11:8]};
                            default: index = {4'b0, lookup_value[15:12]};
                        endcase
                    end else begin
                        index = tex_u[0] ? lookup_value[15:8] : lookup_value[7:0];
                    end
                    clut_x = {1'b0, clut[5:0], 4'b0} +
                        {3'b0, index};
                    clut_pixel = {clut[14:6], clut_x[9:0]};
                    lookup_addr <= VRAM_WORD_BASE +
                        {{(ADDR_BITS-15){1'b0}}, clut_pixel[18:4]};
                    lookup_lane <= clut_pixel[3:0];
                    resume_state <= S_TEX_CLUT;
                    state <= S_LOOKUP;
                end
            end

            S_TEX_CLUT: begin
                texel <= lookup_value;
                state <= S_TEX_READY;
            end

            S_TEX_READY: begin
                if ((texel & 16'h7fff) == 0) begin
                    advance_pixel();
                end else if (triangle_mode && !raw_texture) begin
                    mod_texel <= texel;
                    mod_red <= attr_q[0];
                    mod_green <= attr_q[1];
                    mod_blue <= attr_q[2];
                    state <= S_TEX_MULT;
                end else begin
                    pending_pixel <= texel |
                        (mask_set ? 16'h8000 : 16'h0000);
                    destination_pixel <= current_pixel_index;
                    if (mask_test) begin
                        lookup_addr <= VRAM_WORD_BASE +
                            {{(ADDR_BITS-15){1'b0}},
                            current_pixel_index[18:4]};
                        lookup_lane <= current_x[3:0];
                        resume_state <= S_MASK_READY;
                        state <= S_LOOKUP;
                    end else begin
                        state <= S_PIXEL_COMMIT;
                    end
                end
            end

            S_TEX_MULT: begin
                mod_product_red <=
                    $signed({1'b0, mod_texel[4:0], 3'b0}) * mod_red;
                mod_product_green <=
                    $signed({1'b0, mod_texel[9:5], 3'b0}) * mod_green;
                mod_product_blue <=
                    $signed({1'b0, mod_texel[14:10], 3'b0}) * mod_blue;
                state <= S_TEX_SHADE;
            end

            S_TEX_SHADE: begin
                pending_pixel <= shade15(
                    mod_product_red >>> 7,
                    mod_product_green >>> 7,
                    mod_product_blue >>> 7) |
                    (mask_set ? 16'h8000 : 16'h0000);
                destination_pixel <= current_pixel_index;
                if (mask_test) begin
                    lookup_addr <= VRAM_WORD_BASE +
                        {{(ADDR_BITS-15){1'b0}},
                        current_pixel_index[18:4]};
                    lookup_lane <= current_x[3:0];
                    resume_state <= S_MASK_READY;
                    state <= S_LOOKUP;
                end else begin
                    state <= S_PIXEL_COMMIT;
                end
            end

            S_MASK_READY: begin
                if (lookup_value[15])
                    advance_pixel();
                else
                    state <= S_PIXEL_COMMIT;
            end

            S_PIXEL_COMMIT: begin
                if (out_valid && out_addr !=
                    VRAM_WORD_BASE + {{(ADDR_BITS-15){1'b0}},
                    destination_pixel[18:4]}) begin
                    after_flush <= S_PIXEL_COMMIT;
                    state <= S_FLUSH_CMD;
                end else begin
                    out_valid <= 1'b1;
                    out_addr <= VRAM_WORD_BASE +
                        {{(ADDR_BITS-15){1'b0}}, destination_pixel[18:4]};
                    out_data[destination_pixel[3:0]*16 +: 16] <= pending_pixel;
                    out_we[destination_pixel[3:0]*2 +: 2] <= 2'b11;
                    pixels <= pixels + 1;
                    advance_pixel();
                end
            end

            S_FLUSH_CMD: if (mem_cmd_ready)
                state <= S_FLUSH_DATA;

            S_FLUSH_DATA: if (mem_wdata_ready) begin
                if (cache0_valid && cache0_addr == out_addr)
                    cache0_valid <= 1'b0;
                if (cache1_valid && cache1_addr == out_addr)
                    cache1_valid <= 1'b0;
                out_valid <= 1'b0;
                out_we <= 0;
                state <= after_flush;
            end

            S_FINISH: begin
                if (out_valid) begin
                    after_flush <= S_DONE;
                    state <= S_FLUSH_CMD;
                end else begin
                    state <= S_DONE;
                end
            end

            S_DONE: begin
                primitives <= primitives + 1;
                state <= S_IDLE;
            end

            default: begin
                error <= 1'b1;
                state <= S_IDLE;
            end
        endcase

        if (rst) begin
            state <= S_IDLE;
            error <= 1'b0;
            primitives <= 0;
            pixels <= 0;
            out_valid <= 1'b0;
            out_we <= 0;
            cache0_valid <= 1'b0;
            cache1_valid <= 1'b0;
            cache_replace <= 1'b0;
            triangle_load <= '0;
            rectangle_load <= '0;
        end
    end
endmodule
