# ROM Makefile
#
# Two stages: assemble and compile to ELF objects, then link them into the
# raw 256KB image with vlink and rom.ld. The ROM was assembled straight to a
# binary with `org` until C entered the picture - linking is what lets vbcc
# objects join in, and it produced a byte-identical image on the switch.

ASM   = vasmm68k_mot
CC68  = vbccm68k
LINK  = vlink

AFLAGS = -Felf -m68000 -no-opt -I.
CFLAGS = -cpu=68000 -O=1 -I../shared
LFLAGS = -b rawbin1 -T rom.ld

BUILD = build
ROM = $(BUILD)/kick.rom
MAP = $(BUILD)/kick.map
SYM = $(BUILD)/kick.sym

# bootstrap.s includes every other .s, so it assembles as one object. They
# are still listed as prerequisites so a change to any of them rebuilds.
ASM_MAIN = bootstrap.s
ASM_INC  = $(filter-out $(ASM_MAIN),$(wildcard *.s))
INCS     = $(wildcard *.i)

# C sources, each compiled to assembly by vbcc then assembled to ELF.
CSRC = sprintf.c serial.c ide.c disk.c memory.c autoconfig.c

# vbcc calls __divu for the / and % in %d. That lives in the kernel's
# libsup.s, which is position independent and already covered by the
# libsup.* tests - shared rather than reimplemented here.
LIBSUP_SRC = ../kernel/libsup.s

# Shared with the kernel. serial_hw.c is the UART register layer; the
# polling versus interrupt strategy is NOT shared - see serial_hw.h.
SHARED_SRC = ../shared/serial_hw.c ../shared/ata.c \
             ../shared/rdb.c ../shared/fat16.c ../shared/ext2.c \
             ../shared/memmap.c
SOBJ = $(addprefix $(BUILD)/,$(notdir $(SHARED_SRC:.c=.o)))

OBJ = $(BUILD)/bootstrap.o $(CSRC:%.c=$(BUILD)/%.o) $(SOBJ) $(BUILD)/libsup.o \
      $(BUILD)/ata_pio.o $(BUILD)/le.o

.PHONY: all clean

all: $(ROM) $(SYM)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/bootstrap.o: $(ASM_MAIN) $(ASM_INC) $(INCS) | $(BUILD)
	$(ASM) $(AFLAGS) -o $@ $(ASM_MAIN)

# The PIO data loops, in assembly because every byte on the disk goes through
# them. Shared with the kernel, like the ATA code that calls them.
$(BUILD)/ata_pio.o: ../shared/ata_pio.s | $(BUILD)
	$(ASM) $(AFLAGS) -o $@ $<

$(BUILD)/le.o: ../shared/le.s | $(BUILD)
	$(ASM) $(AFLAGS) -o $@ $<

$(BUILD)/libsup.o: $(LIBSUP_SRC) | $(BUILD)
	$(ASM) $(AFLAGS) -o $@ $<

# No per-file dependency tracking: vbcc has no -MD. Every C object depends
# on every header instead - see the same rule in ../kernel/Makefile.
HEADERS = $(wildcard *.h ../shared/*.h)
$(CSRC:%.c=$(BUILD)/%.asm) $(SOBJ:.o=.asm): $(HEADERS)

$(BUILD)/%.asm: %.c | $(BUILD)
	$(CC68) $(CFLAGS) -o=$@ $<

$(BUILD)/%.asm: ../shared/%.c | $(BUILD)
	$(CC68) $(CFLAGS) -o=$@ $<

$(BUILD)/%.o: $(BUILD)/%.asm
	$(ASM) $(AFLAGS) -o $@ $<

# vlink -M writes the map to stdout; it is the symbol source for the tests.
$(ROM) $(MAP): $(OBJ) rom.ld | $(BUILD)
	$(LINK) $(LFLAGS) -M -o $(ROM) $(OBJ) > $(MAP)
	@echo "ROM size: $$(wc -c < $(ROM)) bytes"

# Flat symbol table for the headless tests in tests/. The ROM is a raw binary
# with no symbols of its own, so they come out of the linker map - which,
# unlike a single assembly listing, covers the C objects too.
$(SYM): $(MAP)
	@python3 ../../tests/mksym.py $(MAP) $(SYM)

clean:
	rm -rf $(BUILD)
