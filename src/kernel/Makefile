# Kernel Makefile

# Tools
VBCC    = vbccm68k
VASM    = vasmm68k_mot
VLINK   = vlink

# Flags
VBCCFLAGS = -cpu=68000 -O=1 -I../shared
VASMFLAGS = -m68000 -Felf -quiet
VLINKFLAGS = -b rawbin1

# Directories
BUILD   = build

# Output
TARGET  = $(BUILD)/SYSTEM.BIN
MAP     = $(BUILD)/kernel.map
SYM     = $(BUILD)/kernel.sym

# Sources
ASRC    = crt0.s libsup.s cpu.s isr.s vectors.s
CSRC    = kernel.c mem.c serial.c kprintf.c irq.c

# Shared with the ROM: hardware definitions and the UART register
# primitives. The waiting strategy is not shared - see serial_hw.h.
SHARED_SRC = ../shared/serial_hw.c

# Objects
AOBJ    = $(ASRC:%.s=$(BUILD)/%.o)
COBJ    = $(CSRC:%.c=$(BUILD)/%.o)
SOBJ    = $(addprefix $(BUILD)/,$(notdir $(SHARED_SRC:.c=.o)))
CASM    = $(CSRC:%.c=$(BUILD)/%.asm)
OBJ     = $(AOBJ) $(COBJ) $(SOBJ)

# Rules
all: $(TARGET) $(SYM)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): $(OBJ) kernel.ld | $(BUILD)
	$(VLINK) $(VLINKFLAGS) -T kernel.ld -M -o $@ $(OBJ) > $(MAP)

# Flat symbol table for the headless tests, which load this image and call
# into it by name. Same route as the ROM: out of the linker map.
$(SYM): $(TARGET)
	@python3 ../../tests/mksym.py $(MAP) $(SYM)

$(BUILD)/%.o: %.s | $(BUILD)
	$(VASM) $(VASMFLAGS) -o $@ $<

# No per-file dependency tracking: vbcc has no -MD. Every C object depends
# on every header instead, which is cheap at this size and cannot go stale -
# a changed struct in ../shared must never leave an old object in the link.
HEADERS = $(wildcard *.h ../shared/*.h)
$(CASM) $(SOBJ:.o=.asm): $(HEADERS)

$(BUILD)/%.asm: %.c | $(BUILD)
	$(VBCC) $(VBCCFLAGS) -o=$@ $<

$(BUILD)/%.asm: ../shared/%.c | $(BUILD)
	$(VBCC) $(VBCCFLAGS) -o=$@ $<

$(BUILD)/%.o: $(BUILD)/%.asm | $(BUILD)
	$(VASM) $(VASMFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: all clean
