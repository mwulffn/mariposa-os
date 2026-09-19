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

# Sources
ASRC    = crt0.s libsup.s
CSRC    = kernel.c mem.c serial.c kprintf.c

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
all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): $(OBJ) kernel.ld | $(BUILD)
	$(VLINK) $(VLINKFLAGS) -T kernel.ld -o $@ $(OBJ)

$(BUILD)/%.o: %.s | $(BUILD)
	$(VASM) $(VASMFLAGS) -o $@ $<

$(BUILD)/%.asm: %.c | $(BUILD)
	$(VBCC) $(VBCCFLAGS) -o=$@ $<

$(BUILD)/%.asm: ../shared/%.c | $(BUILD)
	$(VBCC) $(VBCCFLAGS) -o=$@ $<

$(BUILD)/%.o: $(BUILD)/%.asm | $(BUILD)
	$(VASM) $(VASMFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: all clean
