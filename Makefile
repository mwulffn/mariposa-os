# Amiga bare-metal development Makefile

ASM = vasmm68k_mot
AFLAGS = -Fbin -m68000 -no-opt -I$(SRCDIR)

SRCDIR = src/rom
BUILDDIR = build

ROM = $(BUILDDIR)/kick.rom
SRCS = $(wildcard $(SRCDIR)/*.s)
INCS = $(wildcard $(SRCDIR)/*.i)

.PHONY: all run clean

all: $(ROM)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Main ROM build - bootstrap.s includes other modules
$(ROM): $(SRCS) $(INCS) | $(BUILDDIR)
	$(ASM) $(AFLAGS) -o $@ $(SRCDIR)/bootstrap.s
	@echo "ROM size: $$(wc -c < $@) bytes"

run: $(ROM)
	/Applications/FS-UAE.app/Contents/MacOS/fs-uae "$(PWD)/a500.fs-uae"

# Alternative: use macOS open command (doesn't pass args reliably)
run-open: $(ROM)
	open -a "FS-UAE" --args "$(PWD)/a500.fs-uae"

clean:
	rm -rf $(BUILDDIR)
