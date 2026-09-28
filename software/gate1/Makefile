# SPDX-License-Identifier: BSD-2-Clause

include ../include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak

OBJECTS = main.o crt0.o
CFLAGS += -I$(BIOS_DIRECTORY)/../common
LIBS = libc libcompiler_rt libbase
LIBRARY_ARCHIVES = $(foreach lib,$(LIBS),../$(lib)/$(lib).a)

all: bios.bin

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

bios.elf: $(BIOS_DIRECTORY)/linker.ld $(OBJECTS) $(LIBRARY_ARCHIVES)
	$(CC) $(LDFLAGS) -T $(BIOS_DIRECTORY)/linker.ld -N -o $@ \
		$(OBJECTS) $(PACKAGES:%=-L../%) \
		-Wl,--start-group $(LIBS:lib%=-l%) -Wl,--end-group \
		-Wl,--gc-sections -Wl,-Map,$@.map

VPATH = $(BIOS_DIRECTORY):$(CPU_DIRECTORY)

%.o: %.c
	$(compile)

%.o: %.S
	$(assemble)

clean:
	$(RM) $(OBJECTS) bios.elf bios.elf.map bios.bin

.PHONY: all clean
