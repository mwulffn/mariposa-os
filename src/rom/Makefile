# ROM Makefile

ASM = vasmm68k_mot
AFLAGS = -Fbin -m68000 -no-opt -I.

BUILD = build
ROM = $(BUILD)/kick.rom

SRCS = $(wildcard *.s)
INCS = $(wildcard *.i)

.PHONY: all clean

all: $(ROM)

$(BUILD):
	mkdir -p $(BUILD)

$(ROM): $(SRCS) $(INCS) | $(BUILD)
	$(ASM) $(AFLAGS) -o $@ bootstrap.s
	@echo "ROM size: $$(wc -c < $@) bytes"

clean:
	rm -rf $(BUILD)
