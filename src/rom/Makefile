# ROM Makefile

ASM = vasmm68k_mot
AFLAGS = -Fbin -m68000 -no-opt -I.

BUILD = build
ROM = $(BUILD)/kick.rom
LST = $(BUILD)/kick.lst
SYM = $(BUILD)/kick.sym

SRCS = $(wildcard *.s)
INCS = $(wildcard *.i)

.PHONY: all clean

all: $(ROM) $(SYM)

$(BUILD):
	mkdir -p $(BUILD)

$(ROM) $(LST): $(SRCS) $(INCS) | $(BUILD)
	$(ASM) $(AFLAGS) -L $(LST) -o $(ROM) bootstrap.s
	@echo "ROM size: $$(wc -c < $(ROM)) bytes"

# Flat symbol table for the headless tests in tests/. The ROM is a raw
# binary with no symbols of its own, so they come out of vasm's listing.
$(SYM): $(LST)
	@python3 ../../tests/mksym.py $(LST) $(SYM)

clean:
	rm -rf $(BUILD)
