// SPDX-License-Identifier: GPL-3.0-only

#include "machine.h"

static uint32_t load_le(const uint8_t *data, uint32_t bytes)
{
	uint32_t value = data[0];
	if (bytes > 1u)
		value |= (uint32_t)data[1] << 8;
	if (bytes > 2u)
		value |= (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
	return value;
}

static void store_le(uint8_t *data, uint32_t bytes, uint32_t value)
{
	data[0] = (uint8_t)value;
	if (bytes > 1u)
		data[1] = (uint8_t)(value >> 8);
	if (bytes > 2u) {
		data[2] = (uint8_t)(value >> 16);
		data[3] = (uint8_t)(value >> 24);
	}
}

static uint32_t part_read(uint32_t value, uint32_t address, uint32_t bytes)
{
	value >>= (address & 3u) * 8u;
	if (bytes == 1u)
		return value & 0xffu;
	if (bytes == 2u)
		return value & 0xffffu;
	return value;
}

static uint32_t part_write(uint32_t old, uint32_t address, uint32_t bytes,
	uint32_t value)
{
	uint32_t shift = (address & 3u) * 8u;
	uint32_t mask = bytes == 1u ? 0xffu :
		(bytes == 2u ? 0xffffu : 0xffffffffu);
	return (old & ~(mask << shift)) | ((value & mask) << shift);
}

static uint32_t ram_read32(const struct psx_machine *machine, uint32_t address)
{
	uint32_t offset = address & (PSX_MAIN_RAM_BYTES - 1u);
	return load_le(&machine->ram[offset], 4);
}

static void ram_write32(struct psx_machine *machine, uint32_t address,
	uint32_t value)
{
	uint32_t offset = address & (PSX_MAIN_RAM_BYTES - 1u);
	store_le(&machine->ram[offset], 4, value);
}

static void update_dma_irq(struct psx_machine *machine)
{
	uint32_t pending = (machine->dma_interrupt >> 24) & 0x7fu;
	uint32_t enabled = (machine->dma_interrupt >> 16) & 0x7fu;
	uint32_t active = (machine->dma_interrupt & (1u << 15)) ||
		((machine->dma_interrupt & (1u << 23)) && (pending & enabled));
	if (active) {
		machine->dma_interrupt |= 1u << 31;
		machine->irq_status |= 1u << 3;
	} else {
		machine->dma_interrupt &= ~(1u << 31);
	}
}

static void cd_set_response(struct psx_machine *machine, uint8_t interrupt,
	const uint8_t *response, uint32_t count)
{
	uint32_t i;
	machine->cd_response_read = 0;
	machine->cd_response_count = (uint8_t)count;
	for (i = 0; i < count; ++i)
		machine->cd_response[i] = response[i];
	machine->cd_irq_flag = interrupt;
	if (machine->cd_irq_enable & (1u << (interrupt - 1u)))
		machine->irq_status |= 1u << 2;
}

static int cd_has_second_stage(uint8_t command)
{
	return command == 0x07u || command == 0x08u || command == 0x09u ||
		command == 0x0au || command == 0x12u || command == 0x15u ||
		command == 0x16u || command == 0x1au || command == 0x1eu;
}

static void cd_complete_command(struct psx_machine *machine)
{
	uint8_t response[8];
	uint32_t count = 1;
	uint8_t interrupt = machine->cd_pending_stage == 2u ? 2u : 3u;
	uint8_t command = machine->cd_pending_command;

	response[0] = machine->cd_drive_status;
	if (machine->cd_pending_stage == 2u && command == 0x1au) {
		/* No disc: GetID's second response is an INT5 error. */
		response[1] = 0x80u;
		count = 2;
		interrupt = 5;
	} else if (command == 0x0fu) {
		response[1] = machine->cd_mode;
		response[2] = 0;
		response[3] = 0;
		count = 4;
	} else if (command == 0x13u) {
		response[1] = 1;
		response[2] = 1;
		count = 3;
	} else if (command == 0x14u) {
		response[1] = 0;
		response[2] = 2;
		count = 3;
	} else if (command == 0x19u && machine->cd_parameter_count != 0u &&
		   machine->cd_parameters[0] == 0x20u) {
		response[0] = 0x94u;
		response[1] = 0x09u;
		response[2] = 0x19u;
		response[3] = 0xc0u;
		count = 4;
	}
	if (command == 0x0eu && machine->cd_parameter_count != 0u)
		machine->cd_mode = machine->cd_parameters[0];
	cd_set_response(machine, interrupt, response, count);
	if (machine->cd_pending_stage == 1u && cd_has_second_stage(command)) {
		machine->cd_pending_stage = 2;
		machine->cd_deadline = machine->cpu.cycles + 2000u;
	} else {
		machine->cd_pending_stage = 0;
	}
}

static void cd_start_command(struct psx_machine *machine, uint8_t command)
{
	machine->cd_commands[machine->cd_command_count & 31u] = command;
	++machine->cd_command_count;
	machine->cd_pending_command = command;
	machine->cd_pending_stage = 1;
	machine->cd_deadline = machine->cpu.cycles + 200u;
}

static void cd_service(struct psx_machine *machine)
{
	if (machine->cd_pending_stage != 0u && machine->cd_irq_flag == 0u &&
	    (int32_t)(machine->cpu.cycles - machine->cd_deadline) >= 0)
		cd_complete_command(machine);
}

static void complete_dma(struct psx_machine *machine, uint32_t channel)
{
	machine->dma[channel].control &= ~((1u << 24) | (1u << 28));
	machine->dma_interrupt |= 1u << (24u + channel);
	update_dma_irq(machine);
}

static void run_dma_gpu(struct psx_machine *machine)
{
	struct psx_dma_channel *dma = &machine->dma[2];
	uint32_t address = dma->base & 0x1ffffcu;
	uint32_t sync = (dma->control >> 9) & 3u;
	uint32_t words;
	uint32_t n;

	if (sync == 2u) {
		uint32_t packets = 0;
		for (;;) {
			uint32_t header = ram_read32(machine, address);
			words = header >> 24;
			for (n = 0; n < words; ++n) {
				address = (address + 4u) & 0x1ffffcu;
				psx_gpu_write_gp0(&machine->gpu,
					ram_read32(machine, address));
			}
			machine->dma_words += words;
			if (header & 0x00800000u)
				break;
			address = header & 0x1ffffcu;
			if (++packets >= 65536u)
				break;
		}
	} else {
		words = sync == 0u ? (dma->block & 0xffffu) :
			(dma->block & 0xffffu) * (dma->block >> 16);
		if (words == 0u)
			words = 0x10000u;
		for (n = 0; n < words; ++n) {
			if (dma->control & 1u)
				psx_gpu_write_gp0(&machine->gpu,
					ram_read32(machine, address));
			else
				ram_write32(machine, address,
					psx_gpu_read_data(&machine->gpu));
			address = (address + ((dma->control & 2u) ? -4u : 4u)) &
				0x1ffffcu;
		}
		machine->dma_words += words;
	}
	dma->base = address;
	complete_dma(machine, 2);
}

static void run_dma_otc(struct psx_machine *machine)
{
	struct psx_dma_channel *dma = &machine->dma[6];
	uint32_t address = dma->base & 0x1ffffcu;
	uint32_t words = dma->block & 0xffffu;
	uint32_t n;

	if (words == 0u)
		words = 0x10000u;
	for (n = 0; n < words; ++n) {
		uint32_t value = n + 1u == words ? 0x00ffffffu :
			((address - 4u) & 0x1fffffu);
		ram_write32(machine, address, value);
		address = (address - 4u) & 0x1ffffcu;
	}
	machine->dma_words += words;
	dma->base = address;
	complete_dma(machine, 6);
}

static void start_dma(struct psx_machine *machine, uint32_t channel)
{
	uint32_t control = machine->dma[channel].control;
	uint32_t sync = (control >> 9) & 3u;
	if (!(control & (1u << 24)))
		return;
	if (sync == 0u && !(control & (1u << 28)))
		return;
	if (channel == 2u)
		run_dma_gpu(machine);
	else if (channel == 6u)
		run_dma_otc(machine);
	else
		complete_dma(machine, channel);
}

static uint32_t read_io(struct psx_machine *machine, uint32_t address,
	uint32_t bytes)
{
	uint32_t aligned = address & ~3u;
	uint32_t value = 0;

	if (aligned >= 0x1f801000u && aligned <= 0x1f801020u)
		value = machine->memory_control[(aligned - 0x1f801000u) / 4u];
	else if (aligned == 0x1f801060u)
		value = machine->ram_size;
	else if (aligned == 0x1f801070u)
		value = machine->irq_status;
	else if (aligned == 0x1f801074u)
		value = machine->irq_mask;
	else if (aligned >= 0x1f801080u && aligned < 0x1f8010f0u) {
		uint32_t channel = (aligned - 0x1f801080u) >> 4;
		uint32_t reg = ((aligned - 0x1f801080u) >> 2) & 3u;
		if (reg == 0u) value = machine->dma[channel].base;
		else if (reg == 1u) value = machine->dma[channel].block;
		else if (reg == 2u) value = machine->dma[channel].control;
	} else if (aligned == 0x1f8010f0u)
		value = machine->dma_control;
	else if (aligned == 0x1f8010f4u)
		value = machine->dma_interrupt;
	else if (aligned >= 0x1f801100u && aligned < 0x1f801130u) {
		uint32_t timer = (aligned - 0x1f801100u) >> 4;
		uint32_t reg = ((aligned - 0x1f801100u) >> 2) & 3u;
		if (reg == 0u) value = machine->cpu.cycles & 0xffffu;
		else if (reg == 1u) value = machine->timer_mode[timer];
		else if (reg == 2u) value = machine->timer_target[timer];
	} else if (aligned == 0x1f801040u)
		value = 0xffu;
	else if (aligned == 0x1f801044u)
		value = 5u;
	else if (aligned == 0x1f801810u)
		value = psx_gpu_read_data(&machine->gpu);
	else if (aligned == 0x1f801814u)
		value = psx_gpu_read_status(&machine->gpu);
	else if (address >= 0x1f801800u && address < 0x1f801804u) {
		if (address == 0x1f801800u) {
			value = machine->cd_index | 0x18u;
			if (machine->cd_response_read < machine->cd_response_count)
				value |= 1u << 5;
		} else if (address == 0x1f801801u) {
			if (machine->cd_response_read < machine->cd_response_count)
				value = machine->cd_response[machine->cd_response_read++];
		} else if (address == 0x1f801803u) {
			value = machine->cd_index == 0u ?
				(machine->cd_irq_enable | 0xe0u) :
				(machine->cd_irq_flag | 0xe0u);
		}
		return value;
	}
	else if (aligned == 0x1f801824u)
		value = 0x80040000u;
	else if (address >= 0x1f801c00u && address < 0x1f802000u) {
		uint32_t index = (address - 0x1f801c00u) >> 1;
		value = machine->spu[index];
		if (address == 0x1f801daeu)
			value = (value & ~0x3fu) |
				(machine->spu[0x1aau >> 1] & 0x3fu);
		return bytes == 1u ? value & 0xffu : value;
	}
	else {
		++machine->unknown_reads;
		machine->last_unknown_read = address;
	}
	return part_read(value, address, bytes);
}

static void write_io(struct psx_machine *machine, uint32_t address,
	uint32_t bytes, uint32_t value)
{
	uint32_t aligned = address & ~3u;

	if (aligned >= 0x1f801000u && aligned <= 0x1f801020u) {
		uint32_t reg = (aligned - 0x1f801000u) / 4u;
		machine->memory_control[reg] = part_write(
			machine->memory_control[reg], address, bytes, value);
	} else if (aligned == 0x1f801060u) {
		machine->ram_size = part_write(machine->ram_size, address, bytes,
			value);
	} else if (aligned == 0x1f801070u) {
		machine->irq_status &= part_write(0xffffffffu, address, bytes, value);
	} else if (aligned == 0x1f801074u) {
		machine->irq_mask = part_write(machine->irq_mask, address, bytes,
			value) & 0x7ffu;
	} else if (aligned >= 0x1f801080u && aligned < 0x1f8010f0u) {
		uint32_t channel = (aligned - 0x1f801080u) >> 4;
		uint32_t reg = ((aligned - 0x1f801080u) >> 2) & 3u;
		if (reg == 0u)
			machine->dma[channel].base = part_write(
				machine->dma[channel].base, address, bytes, value);
		else if (reg == 1u)
			machine->dma[channel].block = part_write(
				machine->dma[channel].block, address, bytes, value);
		else if (reg == 2u) {
			machine->dma[channel].control = part_write(
				machine->dma[channel].control, address, bytes, value);
			start_dma(machine, channel);
		}
	} else if (aligned == 0x1f8010f0u) {
		machine->dma_control = part_write(machine->dma_control, address,
			bytes, value);
	} else if (aligned == 0x1f8010f4u) {
		uint32_t written = part_write(0, address, bytes, value);
		machine->dma_interrupt = (machine->dma_interrupt & 0xff000000u) |
			(written & 0x00ffffffu);
		machine->dma_interrupt &= ~(written & 0x7f000000u);
		update_dma_irq(machine);
	} else if (aligned >= 0x1f801100u && aligned < 0x1f801130u) {
		uint32_t timer = (aligned - 0x1f801100u) >> 4;
		uint32_t reg = ((aligned - 0x1f801100u) >> 2) & 3u;
		if (reg == 1u)
			machine->timer_mode[timer] = part_write(
				machine->timer_mode[timer], address, bytes, value);
		else if (reg == 2u)
			machine->timer_target[timer] = part_write(
				machine->timer_target[timer], address, bytes, value);
	} else if (aligned == 0x1f801810u) {
		psx_gpu_write_gp0(&machine->gpu, value);
	} else if (aligned == 0x1f801814u) {
		psx_gpu_write_gp1(&machine->gpu, value);
	} else if (address >= 0x1f801c00u && address < 0x1f802000u) {
		uint32_t index = (address - 0x1f801c00u) >> 1;
		if (bytes == 2u)
			machine->spu[index] = (uint16_t)value;
		else if (bytes == 1u) {
			uint16_t shift = (uint16_t)((address & 1u) * 8u);
			machine->spu[index] = (uint16_t)((machine->spu[index] &
				~(0xffu << shift)) | ((value & 0xffu) << shift));
		}
	} else if (address >= 0x1f801800u && address < 0x1f801804u) {
		if (address == 0x1f801800u) {
			machine->cd_index = (uint8_t)value & 3u;
		} else if (address == 0x1f801801u && machine->cd_index == 0u) {
			cd_start_command(machine, (uint8_t)value);
		} else if (address == 0x1f801802u && machine->cd_index == 0u) {
			if (machine->cd_parameter_count < 16u)
				machine->cd_parameters[machine->cd_parameter_count++] =
					(uint8_t)value;
		} else if (address == 0x1f801802u && machine->cd_index == 1u) {
			machine->cd_irq_enable = (uint8_t)value & 0x1fu;
			if (machine->cd_irq_flag != 0u &&
			    (machine->cd_irq_enable &
			     (1u << (machine->cd_irq_flag - 1u))))
				machine->irq_status |= 1u << 2;
		} else if (address == 0x1f801803u && machine->cd_index == 1u) {
			machine->cd_irq_flag &= ~((uint8_t)value & 0x1fu);
			if (value & 0x40u)
				machine->cd_parameter_count = 0;
			machine->irq_status &= ~(1u << 2);
		}
	} else if ((address >= 0x1f801040u && address < 0x1f801050u) ||
		   aligned == 0x1f801820u) {
		/* Serial and MDEC command writes are accepted for now. */
	} else if (address == 0x1f802041u || address == 0x1f802042u) {
		machine->bios_trace[machine->bios_trace_count & 31u] =
			(uint8_t)value;
		machine->bios_trace_pc[machine->bios_trace_count & 31u] =
			machine->cpu.pc - 4u;
		machine->bios_trace_ra[machine->bios_trace_count & 31u] =
			machine->cpu.gpr[31];
		++machine->bios_trace_count;
	} else if (address == 0x1f802080u) {
		/* BIOS debug character port. */
	} else {
		++machine->unknown_writes;
		machine->last_unknown_write = address;
	}
}

static int bus_read(void *opaque, uint32_t address, uint32_t bytes,
	uint32_t *value)
{
	struct psx_machine *machine = opaque;
	uint32_t physical = address & 0x1fffffffu;

	if (physical < 0x00800000u) {
		uint32_t offset = physical & (PSX_MAIN_RAM_BYTES - 1u);
		if (bytes > PSX_MAIN_RAM_BYTES - offset)
			return -1;
		*value = load_le(&machine->ram[offset], bytes);
		return 0;
	}
	if (physical >= 0x1f800000u && physical < 0x1f800400u) {
		uint32_t offset = physical - 0x1f800000u;
		if (bytes > PSX_SCRATCH_BYTES - offset)
			return -1;
		*value = load_le(&machine->scratch[offset], bytes);
		return 0;
	}
	if (physical >= 0x1f801000u && physical < 0x1f802100u) {
		*value = read_io(machine, physical, bytes);
		return 0;
	}
	if (physical >= 0x1fc00000u && physical < 0x1fc80000u) {
		uint32_t offset = physical - 0x1fc00000u;
		if (bytes > PSX_BIOS_BYTES - offset)
			return -1;
		*value = load_le(&machine->bios[offset], bytes);
		return 0;
	}
	if (physical == 0x1ffe0130u) {
		*value = part_read(machine->cache_control, address, bytes);
		return 0;
	}
	if (physical >= 0x1f000000u && physical < 0x1f800000u) {
		*value = bytes == 1u ? 0xffu :
			(bytes == 2u ? 0xffffu : 0xffffffffu);
		return 0;
	}
	++machine->unknown_reads;
	machine->last_unknown_read = address;
	*value = 0;
	return 0;
}

static int bus_write(void *opaque, uint32_t address, uint32_t bytes,
	uint32_t value)
{
	struct psx_machine *machine = opaque;
	uint32_t physical = address & 0x1fffffffu;

	if (physical < 0x00800000u) {
		uint32_t offset = physical & (PSX_MAIN_RAM_BYTES - 1u);
		if (bytes > PSX_MAIN_RAM_BYTES - offset)
			return -1;
		/* Isolated cached stores target the R3000A cache, not main RAM. */
		if ((machine->cpu.cp0[12] & 0x00010000u) &&
		    (address & 0xe0000000u) != 0xa0000000u)
			return 0;
		store_le(&machine->ram[offset], bytes, value);
		return 0;
	}
	if (physical >= 0x1f800000u && physical < 0x1f800400u) {
		uint32_t offset = physical - 0x1f800000u;
		if (bytes > PSX_SCRATCH_BYTES - offset)
			return -1;
		store_le(&machine->scratch[offset], bytes, value);
		return 0;
	}
	if (physical >= 0x1f801000u && physical < 0x1f802100u) {
		write_io(machine, physical, bytes, value);
		return 0;
	}
	if (physical == 0x1ffe0130u) {
		machine->cache_control = part_write(machine->cache_control,
			address, bytes, value);
		return 0;
	}
	if ((physical >= 0x1f000000u && physical < 0x1f800000u) ||
	    (physical >= 0x1fc00000u && physical < 0x1fc80000u))
		return 0;
	++machine->unknown_writes;
	machine->last_unknown_write = address;
	return 0;
}

static void update_interrupts(struct psx_machine *machine)
{
	if (machine->irq_status & machine->irq_mask)
		machine->cpu.cp0[13] |= 1u << 10;
	else
		machine->cpu.cp0[13] &= ~(1u << 10);
}

static int fast_ram_pointer(const struct psx_machine *machine, uint32_t address,
	uint32_t bytes, const uint8_t **pointer)
{
	uint32_t physical = address & 0x1fffffffu;
	uint32_t offset;

	if (physical >= 0x00800000u)
		return -1;
	offset = physical & (PSX_MAIN_RAM_BYTES - 1u);
	if (bytes > PSX_MAIN_RAM_BYTES - offset)
		return -1;
	*pointer = &machine->ram[offset];
	return 0;
}

static int fast_read16(const struct psx_machine *machine, uint32_t address,
	int32_t *value)
{
	const uint8_t *p;
	if (fast_ram_pointer(machine, address, 2u, &p))
		return -1;
	*value = (int16_t)(p[0] | (uint16_t)p[1] << 8);
	return 0;
}

static int fast_read32(const struct psx_machine *machine, uint32_t address,
	uint32_t *value)
{
	const uint8_t *p;
	if (fast_ram_pointer(machine, address, 4u, &p))
		return -1;
	*value = load_le(p, 4u);
	return 0;
}

static int fast_write16(struct psx_machine *machine, uint32_t address,
	uint32_t value)
{
	const uint8_t *read_pointer;
	uint8_t *p;
	if (fast_ram_pointer(machine, address, 2u, &read_pointer))
		return -1;
	p = (uint8_t *)(uintptr_t)read_pointer;
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	return 0;
}

static uint32_t fast_code_hash(const struct psx_machine *machine,
	uint32_t start, uint32_t end)
{
	uint32_t hash = 2166136261u;
	uint32_t n;
	for (n = start; n < end; ++n) {
		hash ^= machine->ram[n];
		hash *= 16777619u;
	}
	return hash;
}

static int fast_bitmap_get(const struct psx_machine *machine, uint32_t bitmap,
	int32_t x, int32_t y, int32_t *value)
{
	int32_t width;
	int32_t height;
	int32_t row_words;
	int32_t bits;
	uint32_t data;
	uint32_t word;
	uint32_t mask;
	int32_t pixels_per_word;
	int32_t remainder;
	int32_t shift;
	int32_t index;

	if (fast_read16(machine, bitmap, &width) ||
	    fast_read16(machine, bitmap + 2u, &height) || x < 0 || y < 0 ||
	    x >= width || y >= height) {
		*value = -1;
		return 0;
	}
	if (fast_read16(machine, bitmap + 4u, &row_words) ||
	    fast_read16(machine, bitmap + 6u, &bits) ||
	    fast_read32(machine, bitmap + 12u, &data) ||
	    bits <= 0 || bits > 32)
		return -1;
	pixels_per_word = 32 / bits;
	if (pixels_per_word == 0)
		return -1;
	remainder = x % pixels_per_word;
	shift = 32 - bits - remainder;
	if (shift < 0)
		return -1;
	index = row_words * y + x / pixels_per_word;
	if (index < 0 || fast_read32(machine, data + (uint32_t)index * 4u,
	    &word))
		return -1;
	mask = 0xffffffffu << (32u - (uint32_t)bits);
	*value = (int32_t)((word & (mask >> remainder)) >> shift);
	return 0;
}

static int fast_bitmap_compare(const struct psx_machine *machine,
	uint32_t bitmap, int32_t x, int32_t y, int32_t width, int32_t height,
	uint32_t bounds, int32_t match, uint32_t *samples, int32_t *result)
{
	int32_t bounds_x;
	int32_t bounds_y;
	int32_t bounds_width;
	int32_t bounds_height;
	int32_t bitmap_width;
	int32_t bitmap_height;
	int32_t scan_x;
	int32_t scan_y;

	if (fast_read16(machine, bounds, &bounds_x) ||
	    fast_read16(machine, bounds + 2u, &bounds_y) ||
	    fast_read16(machine, bounds + 4u, &bounds_width) ||
	    fast_read16(machine, bounds + 6u, &bounds_height) ||
	    fast_read16(machine, bitmap, &bitmap_width) ||
	    fast_read16(machine, bitmap + 2u, &bitmap_height))
		return -1;
	if (x < bounds_x || bounds_x + bounds_width < x + width) {
		*result = 1;
		return 0;
	}
	if (y < bounds_y || bounds_y + bounds_height < y + height) {
		*result = -1;
		return 0;
	}
	if (bitmap_width - x < width) {
		*result = 1;
		return 0;
	}
	if (bitmap_height - y < height) {
		*result = -1;
		return 0;
	}
	for (scan_y = y; scan_y < y + height; ++scan_y) {
		for (scan_x = x; scan_x < x + width; ++scan_x) {
			int32_t pixel;
			if (fast_bitmap_get(machine, bitmap, scan_x, scan_y, &pixel))
				return -1;
			++*samples;
			if (pixel != match) {
				*result = scan_y != 0 ? -1 : 1;
				return 0;
			}
		}
	}
	*result = 0;
	return 0;
}

static int accelerate_logo_bitmap_search(struct psx_machine *machine)
{
	struct psx_cpu *cpu = &machine->cpu;
	uint32_t stack = cpu->gpr[29];
	uint32_t output_x;
	uint32_t output_y;
	uint32_t match_word;
	int32_t bitmap_width;
	int32_t bitmap_height;
	int32_t width = (int16_t)cpu->gpr[6];
	int32_t height = (int16_t)cpu->gpr[7];
	uint32_t comparisons = 0;
	uint32_t samples = 0;
	uint32_t skipped;
	int32_t x;
	int32_t y;
	int32_t result = 0;

	if (fast_code_hash(machine, 0x4a100u, 0x4a1f0u) != 0x7849800du ||
	    fast_code_hash(machine, 0x4a450u, 0x4a55cu) != 0x61b774a8u ||
	    fast_code_hash(machine, 0x4a55cu, 0x4a710u) != 0xcfcc4dc9u ||
	    fast_read16(machine, cpu->gpr[4], &bitmap_width) ||
	    fast_read16(machine, cpu->gpr[4] + 2u, &bitmap_height) ||
	    fast_read32(machine, stack + 16u, &output_x) ||
	    fast_read32(machine, stack + 20u, &output_y) ||
	    fast_read32(machine, stack + 24u, &match_word))
		return 0;
	for (y = 0; y < bitmap_height; ++y) {
		for (x = 0; x < bitmap_width; ++x) {
			int32_t compare;
			++comparisons;
			if (fast_bitmap_compare(machine, cpu->gpr[4], x, y, width,
			    height, cpu->gpr[5], (int32_t)match_word, &samples,
			    &compare))
				return 0;
			if (compare == 0) {
				if (fast_write16(machine, output_x, (uint32_t)x) ||
				    fast_write16(machine, output_y, (uint32_t)y))
					return 0;
				result = 1;
				goto complete;
			}
		}
	}
complete:
	cpu->gpr[2] = (uint32_t)result;
	cpu->gpr[6] = (uint32_t)width;
	cpu->gpr[7] = (uint32_t)height;
	cpu->branch_pc = cpu->pc;
	cpu->pc = cpu->gpr[31];
	cpu->next_pc = cpu->pc + 4u;
	cpu->in_delay_slot = 0;
	cpu->next_delay_slot = 0;
	skipped = 32u + comparisons * 48u + samples * 48u;
	cpu->cycles += skipped;
	machine->accelerated_instructions += skipped - 1u;
	return 1;
}

static int accelerate_delay_loop(struct psx_machine *machine)
{
	static const uint32_t ram_copy_a[] = {
		0x90ae0000u, 0x00c01021u, 0x24c6ffffu, 0x24a50001u,
		0x24840001u, 0x1440fffau, 0xa08effffu,
	};
	static const uint32_t ram_copy_b[] = {
		0x80ae0000u, 0x00c01021u, 0x24a50001u, 0x24840001u,
		0x24c6ffffu, 0x1440fffau, 0xa08effffu,
	};
	static const uint32_t bios_word_copy[] = {
		0x8c870000u, 0x20c6fffcu, 0x20840004u,
		0x20a50004u, 0x14c0fffbu, 0xaca7fffcu,
	};
	static const uint32_t bios_clear[] = {
		0xac400000u, 0x20420004u, 0x0043082bu,
		0x1420fffcu, 0x00000000u,
	};
	static const uint32_t spu_copy[] = {
		0x96020000u, 0x24630002u, 0x0073082au,
		0x26100002u, 0x1420fffbu, 0xa62201a8u,
	};
	static const uint32_t copy_loop[] = {
		0x90ae0000u, 0x24c6ffffu, 0x24a50001u,
		0x24840001u, 0x1cc0fffbu, 0xa08effffu,
	};
	static const uint32_t loop[] = {
		0x000fc080u, 0x030fc023u, 0xafb80000u, 0x8fb90004u,
		0x00000000u, 0x27280001u, 0xafa80004u, 0x8fa90004u,
		0x00000000u, 0x292100f0u, 0x1420fff4u, 0x8faf0000u,
	};
	struct psx_cpu *cpu = &machine->cpu;
	uint32_t physical = cpu->pc & 0x1fffffffu;
	uint32_t stack = cpu->gpr[29] & 0x1fffffffu;
	uint32_t count;
	uint32_t value;
	uint32_t previous = 0;
	uint32_t iterations;
	uint32_t n;

	if (cpu->load_pending || (cpu->cp0[12] & 1u &&
	    (cpu->cp0[12] & cpu->cp0[13] & 0x0000ff00u)))
		return 0;
#ifndef PSX_DISABLE_LOGO_ACCEL
	if (physical == 0x0004a450u && accelerate_logo_bitmap_search(machine))
		return 1;
#endif
	if (physical == 0x000545b8u) {
		uint32_t source = cpu->gpr[16];
		uint32_t index = cpu->gpr[3];
		uint32_t limit = cpu->gpr[19];
		uint32_t iterations = index < limit ?
			(limit - index + 1u) / 2u : 1u;
		uint32_t halfword;
		for (n = 0; n < sizeof(spu_copy) / sizeof(spu_copy[0]); ++n) {
			if (ram_read32(machine, physical + n * 4u) != spu_copy[n])
				return 0;
		}
		if (iterations > PSX_MAIN_RAM_BYTES ||
		    bus_read(machine, source + (iterations - 1u) * 2u, 2u,
			&halfword) ||
		    bus_write(machine, cpu->gpr[17] + 0x1a8u, 2u, halfword))
			return 0;
		cpu->gpr[1] = 0;
		cpu->gpr[2] = halfword;
		cpu->gpr[3] = index + iterations * 2u;
		cpu->gpr[16] = source + iterations * 2u;
		cpu->branch_pc = cpu->pc + 16u;
		cpu->pc += 24u;
		cpu->next_pc = cpu->pc + 4u;
		cpu->in_delay_slot = 0;
		cpu->next_delay_slot = 0;
		n = iterations * 6u;
		cpu->cycles += n;
		machine->accelerated_instructions += n - 1u;
		return 1;
	}
	if (physical == 0x000513d8u || physical == 0x000308c0u) {
		const uint32_t *signature = physical == 0x000513d8u ?
			ram_copy_a : ram_copy_b;
		uint32_t source = cpu->gpr[5];
		uint32_t destination = cpu->gpr[4];
		uint32_t count = cpu->gpr[6] + 1u;
		uint32_t byte = 0;
		if (count == 0u || count > PSX_MAIN_RAM_BYTES)
			return 0;
		for (n = 0; n < sizeof(ram_copy_a) / sizeof(ram_copy_a[0]); ++n) {
			if (ram_read32(machine, physical + n * 4u) != signature[n])
				return 0;
		}
		for (n = 0; n < count; ++n) {
			if (bus_read(machine, source + n, 1u, &byte) ||
			    bus_write(machine, destination + n, 1u, byte))
				return 0;
		}
		cpu->gpr[2] = 0;
		cpu->gpr[4] = destination + count;
		cpu->gpr[5] = source + count;
		cpu->gpr[6] = 0xffffffffu;
		cpu->gpr[14] = byte;
		cpu->branch_pc = cpu->pc + 20u;
		cpu->pc += 28u;
		cpu->next_pc = cpu->pc + 4u;
		cpu->in_delay_slot = 0;
		cpu->next_delay_slot = 0;
		n = count * 7u;
		cpu->cycles += n;
		machine->accelerated_instructions += n - 1u;
		return 1;
	}
	if (physical == 0x1fc00434u && cpu->gpr[6] != 0u &&
	    (cpu->gpr[6] & 3u) == 0u) {
		uint32_t source = cpu->gpr[4];
		uint32_t destination = cpu->gpr[5];
		uint32_t bytes = cpu->gpr[6];
		uint32_t word = 0;
		if (bytes > PSX_MAIN_RAM_BYTES)
			return 0;
		for (n = 0; n < sizeof(bios_word_copy) /
		     sizeof(bios_word_copy[0]); ++n) {
			if (load_le(&machine->bios[0x434u + n * 4u], 4u) !=
			    bios_word_copy[n])
				return 0;
		}
		for (n = 0; n < bytes; n += 4u) {
			if (bus_read(machine, source + n, 4u, &word) ||
			    bus_write(machine, destination + n, 4u, word))
				return 0;
		}
		cpu->gpr[1] = 0;
		cpu->gpr[4] = source + bytes;
		cpu->gpr[5] = destination + bytes;
		cpu->gpr[6] = 0;
		cpu->gpr[7] = word;
		cpu->branch_pc = cpu->pc + 16u;
		cpu->pc += 24u;
		cpu->next_pc = cpu->pc + 4u;
		cpu->in_delay_slot = 0;
		cpu->next_delay_slot = 0;
		n = bytes / 4u * 6u;
		cpu->cycles += n;
		machine->accelerated_instructions += n - 1u;
		return 1;
	}
	if (physical == 0x1fc003b8u && cpu->gpr[2] < cpu->gpr[3]) {
		uint32_t address = cpu->gpr[2];
		uint32_t end = cpu->gpr[3];
		uint32_t iterations = (end - address + 3u) / 4u;
		if (iterations > PSX_MAIN_RAM_BYTES / 4u)
			return 0;
		for (n = 0; n < sizeof(bios_clear) / sizeof(bios_clear[0]); ++n) {
			if (load_le(&machine->bios[0x3b8u + n * 4u], 4u) !=
			    bios_clear[n])
				return 0;
		}
		for (n = 0; n < iterations; ++n) {
			if (bus_write(machine, address + n * 4u, 4u, 0u))
				return 0;
		}
		cpu->gpr[1] = 0;
		cpu->gpr[2] = address + iterations * 4u;
		cpu->branch_pc = cpu->pc + 12u;
		cpu->pc += 20u;
		cpu->next_pc = cpu->pc + 4u;
		cpu->in_delay_slot = 0;
		cpu->next_delay_slot = 0;
		n = iterations * 5u;
		cpu->cycles += n;
		machine->accelerated_instructions += n - 1u;
		return 1;
	}
	if (physical == 0x1fc02b68u && (int32_t)cpu->gpr[6] > 0 &&
	    cpu->gpr[6] <= PSX_MAIN_RAM_BYTES) {
		uint32_t source = cpu->gpr[5];
		uint32_t destination = cpu->gpr[4];
		uint32_t count = cpu->gpr[6];
		uint32_t source_physical = source & 0x1fffffffu;
		uint32_t destination_physical = destination & 0x1fffffffu;
		int source_valid = source_physical < 0x00800000u ?
			count <= 0x00800000u - source_physical :
			(source_physical >= 0x1fc00000u &&
			 count <= 0x1fc80000u - source_physical);
		if (!source_valid || destination_physical >= 0x00800000u ||
		    count > 0x00800000u - destination_physical)
			return 0;
		for (n = 0; n < sizeof(copy_loop) / sizeof(copy_loop[0]); ++n) {
			if (load_le(&machine->bios[0x2b68u + n * 4u], 4u) !=
			    copy_loop[n])
				return 0;
		}
		for (n = 0; n < count; ++n) {
			uint32_t byte;
			if (bus_read(machine, source + n, 1u, &byte) ||
			    bus_write(machine, destination + n, 1u, byte))
				return 0;
			cpu->gpr[14] = byte;
		}
		cpu->gpr[4] = destination + count;
		cpu->gpr[5] = source + count;
		cpu->gpr[6] = 0;
		cpu->branch_pc = cpu->pc + 16u;
		cpu->pc += 24u;
		cpu->next_pc = cpu->pc + 4u;
		cpu->in_delay_slot = 0;
		cpu->next_delay_slot = 0;
		n = count * 6u;
		cpu->cycles += n;
		machine->accelerated_instructions += n - 1u;
		return 1;
	}
	if (physical < 4u || physical + sizeof(loop) > PSX_MAIN_RAM_BYTES ||
	    stack + 8u > PSX_MAIN_RAM_BYTES ||
	    ram_read32(machine, physical - 4u) != 0u)
		return 0;
	for (n = 0; n < sizeof(loop) / sizeof(loop[0]); ++n) {
		if (ram_read32(machine, physical + n * 4u) != loop[n])
			return 0;
	}
	count = ram_read32(machine, stack + 4u);
	if (count >= 240u)
		return 0;
	iterations = 240u - count;
	value = ram_read32(machine, stack);
	for (n = 0; n < iterations; ++n) {
		previous = value;
		value *= 3u;
	}
	ram_write32(machine, stack, value);
	ram_write32(machine, stack + 4u, 240u);
	cpu->gpr[1] = 0;
	cpu->gpr[8] = 240u;
	cpu->gpr[9] = 240u;
	cpu->gpr[15] = previous;
	cpu->gpr[24] = value;
	cpu->gpr[25] = 239u;
	cpu->branch_pc = cpu->pc + 40u;
	cpu->pc += 48u;
	cpu->next_pc = cpu->pc + 4u;
	cpu->in_delay_slot = 0;
	cpu->next_delay_slot = 0;
	cpu->load_pending = 1;
	cpu->load_reg = 15u;
	cpu->load_value = value;
	n = iterations * 13u - 1u;
	cpu->cycles += n;
	machine->accelerated_instructions += n - 1u;
	return 1;
}

void psx_machine_reset(struct psx_machine *machine, uint8_t *ram,
	uint16_t *vram, const uint8_t *bios)
{
	uint32_t i;
	uint8_t *bytes = (uint8_t *)machine;

	for (i = 0; i < sizeof(*machine); ++i)
		bytes[i] = 0;
	for (i = 0; i < PSX_MAIN_RAM_BYTES; ++i)
		ram[i] = 0;
	machine->ram = ram;
	machine->vram = vram;
	machine->bios = bios;
	machine->ram_size = 0x00000b88u;
	machine->dma_control = 0x07654321u;
	machine->cd_drive_status = 0x10u;
	psx_gpu_reset(&machine->gpu, vram);
	psx_cpu_reset_bus(&machine->cpu, bus_read, bus_write, machine,
		0xbfc00000u);
	psx_jit_reset(&machine->jit);
	machine->cpu.ram = ram;
	machine->cpu.ram_size = PSX_MAIN_RAM_BYTES;
	machine->cpu.ram_map_size = 0x00800000u;
	machine->cpu.bios = bios;
	machine->cpu.bios_base = 0x1fc00000u;
	machine->cpu.bios_size = PSX_BIOS_BYTES;
	machine->cpu.cp0[12] = 0x10900000u;
	machine->cpu.cp0[15] = 2u;
}

int psx_machine_step(struct psx_machine *machine)
{
	int result;
	update_interrupts(machine);
	if (accelerate_delay_loop(machine)) {
		cd_service(machine);
		return 0;
	}
	result = psx_cpu_step(&machine->cpu);
	cd_service(machine);
	return result;
}

void psx_machine_vblank(struct psx_machine *machine)
{
	machine->irq_status |= 1u;
	machine->gpu.status ^= 1u << 31;
	++machine->vblanks;
}

int psx_machine_waiting_for_vblank(const struct psx_machine *machine)
{
	static const uint32_t gpu_wait[] = {
		0x8cac0000u, 0x00000000u, 0x006c6826u,
		0x01a27024u, 0x11c0fffbu, 0x00000000u,
	};
	static const uint32_t counter_wait_a[] = {
		0x8fa2001cu, 0x8fb8001cu, 0x00000000u,
		0x2719ffffu, 0x14400008u, 0xafb9001cu,
	};
	static const uint32_t counter_wait_b[] = {
		0x3c088008u, 0x8d089d9cu, 0x00000000u,
		0x0105082au, 0x1420ffeeu, 0x00000000u,
	};
	uint32_t physical = machine->cpu.pc & 0x1fffffffu;
	uint32_t base;
	uint32_t n;

	if (machine->irq_status & 1u)
		return 0;
	if (physical >= 0x00059d54u && physical <= 0x00059d68u) {
		base = 0x00059d54u;
		for (n = 0; n < sizeof(gpu_wait) / sizeof(gpu_wait[0]); ++n) {
			if (ram_read32(machine, base + n * 4u) != gpu_wait[n])
				return 0;
		}
		return 1;
	}
	if (physical < 0x00059dfcu || physical > 0x00059e10u)
		return 0;
	base = 0x00059dc8u;
	for (n = 0; n < sizeof(counter_wait_a) / sizeof(counter_wait_a[0]); ++n) {
		if (ram_read32(machine, base + n * 4u) != counter_wait_a[n])
			return 0;
	}
	base = 0x00059dfcu;
	for (n = 0; n < sizeof(counter_wait_b) / sizeof(counter_wait_b[0]); ++n) {
		if (ram_read32(machine, base + n * 4u) != counter_wait_b[n])
			return 0;
	}
	return (int32_t)ram_read32(machine, 0x00079d9cu) <
		(int32_t)machine->cpu.gpr[5];
}

int psx_machine_run(struct psx_machine *machine, uint32_t instruction_limit)
{
	uint32_t i;
	int result = 0;
	for (i = 0; i < instruction_limit;) {
		uint32_t run;
		int run_result;
		update_interrupts(machine);
		if (accelerate_delay_loop(machine)) {
			cd_service(machine);
			++i;
			continue;
		}
		run = instruction_limit - i;
		if (run > 64u)
			run = 64u;
		/* Preserve the exact entry to the signature-checked logo search. */
		if ((machine->cpu.pc & 0x1fffffffu) >= 0x00049900u &&
		    (machine->cpu.pc & 0x1fffffffu) < 0x00049c40u)
			run = 1u;
		run_result = psx_jit_run(&machine->jit, &machine->cpu, run);
		if (run_result)
			result = run_result;
		cd_service(machine);
		i += run;
	}
	return result;
}

void psx_machine_copy_display(const struct psx_machine *machine,
	volatile uint16_t *output, uint32_t output_width, uint32_t output_height)
{
	uint32_t source_width = machine->gpu.display_width;
	uint32_t source_height = machine->gpu.display_height;
	uint32_t y;
	if (source_width == 0u || source_width > PSX_VRAM_WIDTH)
		source_width = 320u;
	if (source_height == 0u || source_height > PSX_VRAM_HEIGHT)
		source_height = 240u;
	for (y = 0; y < output_height; ++y) {
		uint32_t source_y = (machine->gpu.display_y +
			y * source_height / output_height) & 511u;
		uint32_t x;
		for (x = 0; x < output_width; ++x) {
			uint32_t source_x = (machine->gpu.display_x +
				x * source_width / output_width) & 1023u;
			uint16_t bgr = machine->vram[source_y * 1024u + source_x];
			output[y * output_width + x] = (uint16_t)(
				((bgr & 0x001fu) << 11) |
				((bgr & 0x03e0u) << 1) |
				((bgr & 0x7c00u) >> 10));
		}
	}
}
