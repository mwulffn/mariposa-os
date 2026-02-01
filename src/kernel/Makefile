# Kernel Makefile

# Tools
VBCC    = vbccm68k
VASM    = vasmm68k_mot
VLINK   = vlink

# Flags
VBCCFLAGS = -cpu=68000 -O2
VASMFLAGS = -m68000 -Felf -quiet
VLINKFLAGS = -b rawbin1

# Output
TARGET  = SYSTEM.BIN

# Sources
ASRC    = crt0.s
CSRC    = kernel.c

# Objects
AOBJ    = $(ASRC:.s=.o)
COBJ    = $(CSRC:.c=.o)
CASM    = $(CSRC:.c=.asm)
OBJ     = $(AOBJ) $(COBJ)

# Rules
all: $(TARGET)

$(TARGET): $(OBJ) kernel.ld
	$(VLINK) $(VLINKFLAGS) -T kernel.ld -o $@ $(OBJ)

%.o: %.s
	$(VASM) $(VASMFLAGS) -o $@ $<

%.asm: %.c
	$(VBCC) $(VBCCFLAGS) -o=$@ $<

%.o: %.asm
	$(VASM) $(VASMFLAGS) -o $@ $<

clean:
	rm -f $(OBJ) $(CASM) $(TARGET)

# Explicit dependencies
kernel.o: kernel.asm
kernel.asm: kernel.c
crt0.o: crt0.s

.PHONY: all clean
