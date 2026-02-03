# Amiga Project - Main Makefile

# Platform detection
UNAME_S := $(shell uname -s)

# Configuration
CONFIG = configs/a600.fs-uae
HDD = harddrives/boot.hdf

# Sub-project directories
ROM_DIR = src/rom
KERNEL_DIR = src/kernel

# Build artifacts (for run target)
ROM = $(ROM_DIR)/build/kick.rom
KERNEL = $(KERNEL_DIR)/build/SYSTEM.BIN

# Platform-specific FS-UAE binary path
# Can be overridden with: make run FS_UAE=/path/to/fs-uae
ifeq ($(UNAME_S),Darwin)
    FS_UAE ?= /Applications/FS-UAE.app/Contents/MacOS/fs-uae
else ifeq ($(UNAME_S),Linux)
    FS_UAE ?= fs-uae
else
    FS_UAE ?= fs-uae
endif

.PHONY: all rom kernel deploy run run-open clean

all: rom kernel

rom:
	$(MAKE) -C $(ROM_DIR)

kernel:
	$(MAKE) -C $(KERNEL_DIR)

deploy: kernel
	@echo "Deploying kernel to hard drive image..."
	mcopy -i $(HDD) -o $(KERNEL) ::SYSTEM.BIN
	@echo "Kernel deployed successfully"
	@mdir -i $(HDD) ::

run: rom deploy
	$(FS_UAE) "$(PWD)/$(CONFIG)"

# Alternative: use macOS open command (doesn't pass args reliably, macOS only)
run-open: rom deploy
	open -a "FS-UAE" --args "$(PWD)/$(CONFIG)"

clean:
	$(MAKE) -C $(ROM_DIR) clean
	$(MAKE) -C $(KERNEL_DIR) clean
