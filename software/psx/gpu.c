// SPDX-License-Identifier: GPL-3.0-only

#include "gpu.h"

struct vertex {
	int32_t x;
	int32_t y;
	int32_t r;
	int32_t g;
	int32_t b;
	int32_t u;
	int32_t v;
};

static int32_t sign_extend(uint32_t value, uint32_t bits)
{
	uint32_t mask = 1u << (bits - 1u);
	return (int32_t)((value ^ mask) - mask);
}

static uint16_t color15(uint32_t value)
{
	return (uint16_t)(((value >> 3) & 0x1fu) |
		((value >> 6) & 0x3e0u) | ((value >> 9) & 0x7c00u));
}

static uint32_t color24(uint16_t value)
{
	uint32_t r = (value & 31u) << 3;
	uint32_t g = ((value >> 5) & 31u) << 3;
	uint32_t b = ((value >> 10) & 31u) << 3;
	return r | (g << 8) | (b << 16);
}

static void decode_color(uint32_t value, struct vertex *v)
{
	v->r = value & 0xffu;
	v->g = (value >> 8) & 0xffu;
	v->b = (value >> 16) & 0xffu;
}

static void decode_xy(const struct psx_gpu *gpu, uint32_t value,
	struct vertex *v)
{
	v->x = sign_extend(value & 0x7ffu, 11) + gpu->draw_offset_x;
	v->y = sign_extend((value >> 16) & 0x7ffu, 11) + gpu->draw_offset_y;
}

static void put_pixel(struct psx_gpu *gpu, int32_t x, int32_t y,
	uint16_t value)
{
	uint16_t *destination;

	if (x < (int32_t)gpu->draw_x0 || x > (int32_t)gpu->draw_x1 ||
	    y < (int32_t)gpu->draw_y0 || y > (int32_t)gpu->draw_y1)
		return;
	if ((uint32_t)x >= PSX_VRAM_WIDTH || (uint32_t)y >= PSX_VRAM_HEIGHT)
		return;
	destination = &gpu->vram[(uint32_t)y * PSX_VRAM_WIDTH + (uint32_t)x];
	if ((gpu->mask_bits & 2u) && (*destination & 0x8000u))
		return;
	if (gpu->mask_bits & 1u)
		value |= 0x8000u;
	*destination = value;
}

static uint16_t texture_pixel(const struct psx_gpu *gpu, int32_t u, int32_t v,
	uint32_t clut, uint32_t page)
{
	uint32_t page_x = (page & 15u) * 64u;
	uint32_t page_y = ((page >> 4) & 1u) * 256u;
	uint32_t depth = (page >> 7) & 3u;
	uint32_t x;
	uint16_t packed;
	uint32_t index;

	u &= 0xff;
	v &= 0xff;
	if (depth == 0u) {
		x = (page_x + (uint32_t)u / 4u) & 1023u;
		packed = gpu->vram[((page_y + (uint32_t)v) & 511u) * 1024u + x];
		index = (packed >> (4u * ((uint32_t)u & 3u))) & 15u;
		x = ((clut & 63u) * 16u + index) & 1023u;
		return gpu->vram[(((clut >> 6) & 511u) * 1024u) + x];
	}
	if (depth == 1u) {
		x = (page_x + (uint32_t)u / 2u) & 1023u;
		packed = gpu->vram[((page_y + (uint32_t)v) & 511u) * 1024u + x];
		index = (packed >> (8u * ((uint32_t)u & 1u))) & 255u;
		x = ((clut & 63u) * 16u + index) & 1023u;
		return gpu->vram[(((clut >> 6) & 511u) * 1024u) + x];
	}
	return gpu->vram[((page_y + (uint32_t)v) & 511u) * 1024u +
		((page_x + (uint32_t)u) & 1023u)];
}

static int64_t edge(const struct vertex *a, const struct vertex *b,
	int32_t x, int32_t y)
{
	return (int64_t)(x - a->x) * (b->y - a->y) -
		(int64_t)(y - a->y) * (b->x - a->x);
}

static void draw_triangle(struct psx_gpu *gpu, const struct vertex *a,
	const struct vertex *b, const struct vertex *c, int textured, int raw,
	uint32_t clut, uint32_t page)
{
	int32_t min_x = a->x;
	int32_t max_x = a->x;
	int32_t min_y = a->y;
	int32_t max_y = a->y;
	int64_t area = edge(a, b, c->x, c->y);
	int32_t y;

	if (b->x < min_x) min_x = b->x;
	if (c->x < min_x) min_x = c->x;
	if (b->x > max_x) max_x = b->x;
	if (c->x > max_x) max_x = c->x;
	if (b->y < min_y) min_y = b->y;
	if (c->y < min_y) min_y = c->y;
	if (b->y > max_y) max_y = b->y;
	if (c->y > max_y) max_y = c->y;
	if (area == 0)
		return;
	if (min_x < (int32_t)gpu->draw_x0) min_x = (int32_t)gpu->draw_x0;
	if (max_x > (int32_t)gpu->draw_x1) max_x = (int32_t)gpu->draw_x1;
	if (min_y < (int32_t)gpu->draw_y0) min_y = (int32_t)gpu->draw_y0;
	if (max_y > (int32_t)gpu->draw_y1) max_y = (int32_t)gpu->draw_y1;
	for (y = min_y; y <= max_y; ++y) {
		int32_t x;
		for (x = min_x; x <= max_x; ++x) {
			int64_t wa = edge(b, c, x, y);
			int64_t wb = edge(c, a, x, y);
			int64_t wc = edge(a, b, x, y);
			int32_t r;
			int32_t g;
			int32_t blue;
			uint16_t pixel;
			if ((area > 0 && (wa < 0 || wb < 0 || wc < 0)) ||
			    (area < 0 && (wa > 0 || wb > 0 || wc > 0)))
				continue;
			r = (int32_t)((wa * a->r + wb * b->r + wc * c->r) / area);
			g = (int32_t)((wa * a->g + wb * b->g + wc * c->g) / area);
			blue = (int32_t)((wa * a->b + wb * b->b + wc * c->b) / area);
			if (textured) {
				int32_t u = (int32_t)((wa * a->u + wb * b->u +
					wc * c->u) / area);
				int32_t v = (int32_t)((wa * a->v + wb * b->v +
					wc * c->v) / area);
				uint16_t texel = texture_pixel(gpu, u, v, clut, page);
				uint32_t tex_color;
				if ((texel & 0x7fffu) == 0u)
					continue;
				if (raw) {
					put_pixel(gpu, x, y, texel);
					continue;
				}
				tex_color = color24(texel);
				r = (((int32_t)(tex_color & 0xffu) * r) >> 7);
				g = (((int32_t)((tex_color >> 8) & 0xffu) * g) >> 7);
				blue = (((int32_t)((tex_color >> 16) & 0xffu) * blue) >> 7);
			}
			if (r > 255)
				r = 255;
			if (r < 0)
				r = 0;
			if (g > 255)
				g = 255;
			if (g < 0)
				g = 0;
			if (blue > 255)
				blue = 255;
			if (blue < 0)
				blue = 0;
			pixel = (uint16_t)((r >> 3) | ((g >> 3) << 5) |
				((blue >> 3) << 10));
			put_pixel(gpu, x, y, pixel);
		}
	}
}

static void draw_line(struct psx_gpu *gpu, struct vertex a, struct vertex b)
{
	int32_t dx = b.x > a.x ? b.x - a.x : a.x - b.x;
	int32_t sx = a.x < b.x ? 1 : -1;
	int32_t dy_abs = b.y > a.y ? b.y - a.y : a.y - b.y;
	int32_t dy = -dy_abs;
	int32_t sy = a.y < b.y ? 1 : -1;
	int32_t error = dx + dy;
	int32_t steps = dx > dy_abs ? dx : dy_abs;
	int32_t count = 0;

	for (;;) {
		int32_t r = steps ? a.r + (b.r - a.r) * count / steps : a.r;
		int32_t g = steps ? a.g + (b.g - a.g) * count / steps : a.g;
		int32_t blue = steps ? a.b + (b.b - a.b) * count / steps : a.b;
		put_pixel(gpu, a.x, a.y, (uint16_t)((r >> 3) |
			((g >> 3) << 5) | ((blue >> 3) << 10)));
		if (a.x == b.x && a.y == b.y)
			break;
		if (2 * error >= dy) { error += dy; a.x += sx; }
		if (2 * error <= dx) { error += dx; a.y += sy; }
		++count;
	}
}

static void process_polygon(struct psx_gpu *gpu, uint32_t command)
{
	struct vertex vertices[4];
	uint32_t count = command & 8u ? 4u : 3u;
	int gouraud = (command & 16u) != 0;
	int textured = (command & 4u) != 0;
	int raw = (command & 1u) != 0;
	uint32_t color = gpu->packet[0];
	uint32_t clut = 0;
	uint32_t page = gpu->draw_mode;
	uint32_t index = 1;
	uint32_t n;

	for (n = 0; n < count; ++n) {
		if (n != 0u && gouraud)
			color = gpu->packet[index++];
		decode_color(color, &vertices[n]);
		decode_xy(gpu, gpu->packet[index++], &vertices[n]);
		vertices[n].u = 0;
		vertices[n].v = 0;
		if (textured) {
			uint32_t uv = gpu->packet[index++];
			vertices[n].u = uv & 0xffu;
			vertices[n].v = (uv >> 8) & 0xffu;
			if (n == 0u) clut = uv >> 16;
			if (n == 1u) page = uv >> 16;
		}
	}
	draw_triangle(gpu, &vertices[0], &vertices[1], &vertices[2],
		textured, raw, clut, page);
	if (count == 4u)
		draw_triangle(gpu, &vertices[1], &vertices[2], &vertices[3],
			textured, raw, clut, page);
	++gpu->primitives;
}

static void process_rectangle(struct psx_gpu *gpu, uint32_t command)
{
	struct vertex origin;
	uint32_t index = 1;
	uint32_t uv = 0;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
	int textured = (command & 4u) != 0;

	decode_color(gpu->packet[0], &origin);
	decode_xy(gpu, gpu->packet[index++], &origin);
	if (textured)
		uv = gpu->packet[index++];
	switch ((command >> 3) & 3u) {
	case 0: width = gpu->packet[index] & 0xffffu;
		height = gpu->packet[index] >> 16; break;
	case 1: width = 1; height = 1; break;
	case 2: width = 8; height = 8; break;
	default: width = 16; height = 16; break;
	}
	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			uint16_t pixel;
			if (textured) {
				pixel = texture_pixel(gpu, (uv & 0xffu) + (int32_t)x,
					((uv >> 8) & 0xffu) + (int32_t)y,
					uv >> 16, gpu->draw_mode);
				if ((pixel & 0x7fffu) == 0u)
					continue;
			} else {
				pixel = color15(gpu->packet[0]);
			}
			put_pixel(gpu, origin.x + (int32_t)x,
				origin.y + (int32_t)y, pixel);
		}
	}
	++gpu->primitives;
}

static void process_packet(struct psx_gpu *gpu)
{
	uint32_t opcode = gpu->packet[0] >> 24;
	uint32_t command = opcode & 0x1fu;
	uint32_t type = opcode >> 5;
	uint32_t x;
	uint32_t y;

	if (opcode == 0x02u) {
		uint16_t color = color15(gpu->packet[0]);
		uint32_t start_x = gpu->packet[1] & 0x3ffu;
		uint32_t start_y = (gpu->packet[1] >> 16) & 0x1ffu;
		uint32_t width = gpu->packet[2] & 0x3ffu;
		uint32_t height = (gpu->packet[2] >> 16) & 0x1ffu;
		for (y = 0; y < height; ++y)
			for (x = 0; x < width; ++x)
				gpu->vram[((start_y + y) & 511u) * 1024u +
					((start_x + x) & 1023u)] = color;
		++gpu->primitives;
	} else if (type == 1u) {
		process_polygon(gpu, command);
	} else if (type == 2u && !(command & 8u)) {
		struct vertex a;
		struct vertex b;
		decode_color(gpu->packet[0], &a);
		decode_xy(gpu, gpu->packet[1], &a);
		if (command & 16u) {
			decode_color(gpu->packet[2], &b);
			decode_xy(gpu, gpu->packet[3], &b);
		} else {
			b = a;
			decode_xy(gpu, gpu->packet[2], &b);
		}
		draw_line(gpu, a, b);
		++gpu->primitives;
	} else if (type == 3u) {
		process_rectangle(gpu, command);
	} else if (type == 4u) {
		uint32_t sx = gpu->packet[1] & 0x3ffu;
		uint32_t sy = (gpu->packet[1] >> 16) & 0x1ffu;
		uint32_t dx = gpu->packet[2] & 0x3ffu;
		uint32_t dy = (gpu->packet[2] >> 16) & 0x1ffu;
		uint32_t width = gpu->packet[3] & 0xffffu;
		uint32_t height = gpu->packet[3] >> 16;
		if (width == 0u) width = 0x400u;
		if (height == 0u) height = 0x200u;
		for (y = 0; y < height; ++y)
			for (x = 0; x < width; ++x)
				gpu->vram[((dy + y) & 511u) * 1024u +
					((dx + x) & 1023u)] =
				gpu->vram[((sy + y) & 511u) * 1024u +
					((sx + x) & 1023u)];
	} else if (type == 5u) {
		gpu->image_x = gpu->packet[1] & 0x3ffu;
		gpu->image_y = (gpu->packet[1] >> 16) & 0x1ffu;
		gpu->image_w = gpu->packet[2] & 0xffffu;
		gpu->image_h = gpu->packet[2] >> 16;
		if (gpu->image_w == 0u) gpu->image_w = 0x400u;
		if (gpu->image_h == 0u) gpu->image_h = 0x200u;
		gpu->image_pixel = 0;
		gpu->image_pixels = gpu->image_w * gpu->image_h;
		++gpu->uploads;
	} else if (type == 6u) {
		gpu->image_x = gpu->packet[1] & 0x3ffu;
		gpu->image_y = (gpu->packet[1] >> 16) & 0x1ffu;
		gpu->image_w = gpu->packet[2] & 0xffffu;
		gpu->image_h = gpu->packet[2] >> 16;
		if (gpu->image_w == 0u) gpu->image_w = 0x400u;
		if (gpu->image_h == 0u) gpu->image_h = 0x200u;
		gpu->read_pixel = 0;
		gpu->read_pixels = gpu->image_w * gpu->image_h;
	} else if (type == 7u) {
		switch (command) {
		case 1: gpu->draw_mode = gpu->packet[0] & 0x3fffu; break;
		case 2: gpu->texture_window = gpu->packet[0] & 0xfffffu; break;
		case 3: gpu->draw_x0 = gpu->packet[0] & 0x3ffu;
			gpu->draw_y0 = (gpu->packet[0] >> 10) & 0x1ffu; break;
		case 4: gpu->draw_x1 = gpu->packet[0] & 0x3ffu;
			gpu->draw_y1 = (gpu->packet[0] >> 10) & 0x1ffu; break;
		case 5: gpu->draw_offset_x = sign_extend(gpu->packet[0] & 0x7ffu, 11);
			gpu->draw_offset_y = sign_extend((gpu->packet[0] >> 11) & 0x7ffu, 11); break;
		case 6: gpu->mask_bits = gpu->packet[0] & 3u; break;
		default: ++gpu->unknown_commands; break;
		}
	} else if (opcode != 0u && opcode != 1u) {
		++gpu->unknown_commands;
	}
}

static uint32_t packet_length(uint32_t value)
{
	uint32_t opcode = value >> 24;
	uint32_t type = opcode >> 5;
	uint32_t command = opcode & 0x1fu;

	if (opcode == 0x02u) return 3;
	if (type == 1u) {
		uint32_t vertices = command & 8u ? 4u : 3u;
		return 1u + vertices + ((command & 16u) ? vertices - 1u : 0u) +
			((command & 4u) ? vertices : 0u);
	}
	if (type == 2u)
		return (command & 8u) ? 1u : ((command & 16u) ? 4u : 3u);
	if (type == 3u)
		return 2u + ((command & 4u) ? 1u : 0u) +
			((((command >> 3) & 3u) == 0u) ? 1u : 0u);
	if (type == 4u) return 4;
	if (type == 5u || type == 6u) return 3;
	return 1;
}

void psx_gpu_reset(struct psx_gpu *gpu, uint16_t *vram)
{
	uint32_t i;
	for (i = 0; i < PSX_VRAM_PIXELS; ++i)
		vram[i] = 0;
	gpu->vram = vram;
	gpu->status = 0x14802000u;
	gpu->data_read = 0x400u;
	gpu->packet_words = 0;
	gpu->packet_expected = 0;
	gpu->image_pixels = 0;
	gpu->read_pixels = 0;
	gpu->draw_x0 = 0;
	gpu->draw_y0 = 0;
	gpu->draw_x1 = 1023;
	gpu->draw_y1 = 511;
	gpu->draw_offset_x = 0;
	gpu->draw_offset_y = 0;
	gpu->draw_mode = 0;
	gpu->texture_window = 0;
	gpu->mask_bits = 0;
	gpu->display_x = 0;
	gpu->display_y = 0;
	gpu->display_width = 256;
	gpu->display_height = 240;
	gpu->horizontal_range = 0;
	gpu->vertical_range = 0;
	gpu->command_words = 0;
	gpu->primitives = 0;
	gpu->uploads = 0;
	gpu->unknown_commands = 0;
}

void psx_gpu_write_gp0(struct psx_gpu *gpu, uint32_t value)
{
	++gpu->command_words;
	if (gpu->image_pixel < gpu->image_pixels) {
		uint32_t n;
		for (n = 0; n < 2u && gpu->image_pixel < gpu->image_pixels; ++n) {
			uint32_t pixel = gpu->image_pixel++;
			uint32_t x = (gpu->image_x + pixel % gpu->image_w) & 1023u;
			uint32_t y = (gpu->image_y + pixel / gpu->image_w) & 511u;
			gpu->vram[y * 1024u + x] = (uint16_t)(value >> (16u * n));
		}
		return;
	}
	if (gpu->packet_words == 0u)
		gpu->packet_expected = packet_length(value);
	if (gpu->packet_words < 16u)
		gpu->packet[gpu->packet_words++] = value;
	else {
		gpu->packet_words = 0;
		++gpu->unknown_commands;
		return;
	}
	if (gpu->packet_words == gpu->packet_expected) {
		process_packet(gpu);
		gpu->packet_words = 0;
	}
}

void psx_gpu_write_gp1(struct psx_gpu *gpu, uint32_t value)
{
	uint32_t command = value >> 24;
	if (command == 0u) {
		uint16_t *vram = gpu->vram;
		uint32_t words = gpu->command_words;
		uint32_t primitives = gpu->primitives;
		uint32_t uploads = gpu->uploads;
		psx_gpu_reset(gpu, vram);
		gpu->command_words = words;
		gpu->primitives = primitives;
		gpu->uploads = uploads;
	} else if (command == 1u) {
		gpu->packet_words = 0;
		gpu->image_pixels = 0;
	} else if (command == 2u) {
		gpu->status &= ~(1u << 24);
	} else if (command == 3u) {
		if (value & 1u) gpu->status |= 1u << 23;
		else gpu->status &= ~(1u << 23);
	} else if (command == 4u) {
		gpu->status = (gpu->status & ~(3u << 29)) | ((value & 3u) << 29);
	} else if (command == 5u) {
		gpu->display_x = value & 0x3ffu;
		gpu->display_y = (value >> 10) & 0x1ffu;
	} else if (command == 6u) {
		gpu->horizontal_range = value & 0xffffffu;
	} else if (command == 7u) {
		gpu->vertical_range = value & 0xfffffu;
	} else if (command == 8u) {
		static const uint16_t widths[4] = {256, 320, 512, 640};
		gpu->display_width = (value & 0x40u) ? 368u : widths[value & 3u];
		gpu->display_height = (value & 4u) ? 480u : 240u;
		gpu->status = (gpu->status & ~0x007f0000u) |
			((value & 0x3fu) << 17) | ((value & 0x40u) << 10);
	} else if (command == 16u) {
		switch (value & 7u) {
		case 2: gpu->data_read = 0xe3000000u | gpu->draw_x0 |
			(gpu->draw_y0 << 10); break;
		case 3: gpu->data_read = 0xe4000000u | gpu->draw_x1 |
			(gpu->draw_y1 << 10); break;
		case 4: gpu->data_read = 0xe5000000u |
			((uint32_t)gpu->draw_offset_x & 0x7ffu) |
			(((uint32_t)gpu->draw_offset_y & 0x7ffu) << 11); break;
		default: gpu->data_read = 0; break;
		}
	}
}

uint32_t psx_gpu_read_data(struct psx_gpu *gpu)
{
	uint32_t value;
	uint32_t n;

	if (gpu->read_pixel >= gpu->read_pixels)
		return gpu->data_read;
	value = 0;
	for (n = 0; n < 2u && gpu->read_pixel < gpu->read_pixels; ++n) {
		uint32_t pixel = gpu->read_pixel++;
		uint32_t x = (gpu->image_x + pixel % gpu->image_w) & 1023u;
		uint32_t y = (gpu->image_y + pixel / gpu->image_w) & 511u;
		value |= (uint32_t)gpu->vram[y * 1024u + x] << (16u * n);
	}
	return value;
}

uint32_t psx_gpu_read_status(const struct psx_gpu *gpu)
{
	uint32_t status = gpu->status | 0x1c000000u;
	uint32_t direction = (status >> 29) & 3u;
	if (direction == 1u || direction == 2u)
		status |= 1u << 25;
	return status;
}
