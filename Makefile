# Amiga bare-metal development Makefile

ASM = vasmm68k_mot
AFLAGS = -Fbin -m68000 -no-opt

SRCDIR = src
BUILDDIR = build

ROM = $(BUILDDIR)/kick.rom
SRC = $(SRCDIR)/bootstrap.s

.PHONY: all run clean

all: $(ROM)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(ROM): $(SRC) | $(BUILDDIR)
	$(ASM) $(AFLAGS) -o $@ $<
	@echo "ROM size: $$(wc -c < $@) bytes"

run: $(ROM)
	open -a "FS-UAE" --args "$(PWD)/a500.fs-uae"

# Alternative: invoke the binary directly if 'open' doesn't pass args correctly
run-direct: $(ROM)
	/Applications/FS-UAE.app/Contents/MacOS/fs-uae "$(PWD)/a500.fs-uae"

# Run with vAmiga instead (if installed)
run-vamiga: $(ROM)
	open -a vAmiga $(ROM)

clean:
	rm -rf $(BUILDDIR)
