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
CFLAGS = -cpu=68000 -O=1
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
CSRC =

OBJ = $(BUILD)/bootstrap.o $(CSRC:%.c=$(BUILD)/%.o)

.PHONY: all clean

all: $(ROM) $(SYM)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/bootstrap.o: $(ASM_MAIN) $(ASM_INC) $(INCS) | $(BUILD)
	$(ASM) $(AFLAGS) -o $@ $(ASM_MAIN)

$(BUILD)/%.asm: %.c | $(BUILD)
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
