# Build Tools Installation Guide

This guide covers installing the required build tools for the Amiga OS project on both Linux and macOS.

## Quick Overview

Required tools:
- **vasm** (vasmm68k_mot variant) - 68000 assembler
- **vbcc** (vbccm68k + vlink) - C compiler and linker for 68000
- **mtools** - FAT filesystem manipulation
- **FS-UAE** - Amiga emulator
- **Python 3** - For debug script

## Installation

### Linux (Debian/Ubuntu)

#### 1. Install System Packages

```bash
# Update package manager
sudo apt update

# Install basic build tools
sudo apt install -y build-essential curl wget

# Install mtools for FAT filesystem access
sudo apt install -y mtools

# Install Python 3 (usually pre-installed)
sudo apt install -y python3
```

#### 2. Install VASM

Check if available via package manager first:

```bash
# Try package manager (may not be available on all distros)
sudo apt search vasm

# If not available, build from source:
cd /tmp
wget http://sun.hasenbraten.de/vasm/release/vasm.tar.gz
tar -xzf vasm.tar.gz
cd vasm
make CPU=m68k SYNTAX=mot
mkdir -p ~/.local/bin
cp vasmm68k_mot ~/.local/bin/
cp vobjdump ~/.local/bin/
```

#### 3. Install VBCC

VBCC must be built from source:

```bash
cd /tmp

# Download vbcc source
wget http://www.ibaug.de/vbcc/vbcc.tar.gz
tar -xzf vbcc.tar.gz
cd vbcc

# Build vbcc
mkdir bin
make TARGET=m68k
cp bin/vbccm68k ~/.local/bin/

# Download and build vlink
cd /tmp
wget http://sun.hasenbraten.de/vlink/release/vlink.tar.gz
tar -xzf vlink.tar.gz
cd vlink
make
cp vlink ~/.local/bin/

# Download and install vbcc target libraries
cd /tmp
wget http://www.ibaug.de/vbcc/vbcc_target_m68k-amigaos.tar.gz
mkdir -p ~/.vbcc
tar -xzf vbcc_target_m68k-amigaos.tar.gz -C ~/.vbcc
```

#### 4. Install FS-UAE

```bash
# Try package manager first
sudo apt install fs-uae

# Or download from official website:
# https://fs-uae.net/download
```

#### 5. Add Tools to PATH

Add this to your `~/.bashrc` or `~/.zshrc`:

```bash
export PATH="$HOME/.local/bin:$PATH"
export VBCC="$HOME/.vbcc"
```

Then reload your shell:

```bash
source ~/.bashrc  # or source ~/.zshrc
```

---

### macOS

#### 1. Install Homebrew

If not already installed:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

#### 2. Install System Packages

```bash
# Install mtools
brew install mtools

# Install wget for downloading sources
brew install wget

# Python 3 is usually pre-installed on modern macOS
python3 --version
```

#### 3. Install VASM

```bash
# Check if available via Homebrew
brew search vasm

# If not available, build from source:
cd /tmp
wget http://sun.hasenbraten.de/vasm/release/vasm.tar.gz
tar -xzf vasm.tar.gz
cd vasm
make CPU=m68k SYNTAX=mot
mkdir -p ~/.local/bin
cp vasmm68k_mot ~/.local/bin/
cp vobjdump ~/.local/bin/
```

#### 4. Install VBCC

VBCC must be built from source:

```bash
cd /tmp

# Download vbcc source
wget http://www.ibaug.de/vbcc/vbcc.tar.gz
tar -xzf vbcc.tar.gz
cd vbcc

# Build vbcc
mkdir bin
make TARGET=m68k
cp bin/vbccm68k ~/.local/bin/

# Download and build vlink
cd /tmp
wget http://sun.hasenbraten.de/vlink/release/vlink.tar.gz
tar -xzf vlink.tar.gz
cd vlink
make
cp vlink ~/.local/bin/

# Download and install vbcc target libraries
cd /tmp
wget http://www.ibaug.de/vbcc/vbcc_target_m68k-amigaos.tar.gz
mkdir -p ~/.vbcc
tar -xzf vbcc_target_m68k-amigaos.tar.gz -C ~/.vbcc
```

#### 5. Install FS-UAE

Download and install from the official website:
https://fs-uae.net/download

The default installation path is `/Applications/FS-UAE.app/`

#### 6. Add Tools to PATH

Add this to your `~/.zshrc` or `~/.bash_profile`:

```bash
export PATH="$HOME/.local/bin:$PATH"
export VBCC="$HOME/.vbcc"
```

Then reload your shell:

```bash
source ~/.zshrc  # or source ~/.bash_profile
```

---

## Verify Installation

After installation, verify all tools are accessible:

```bash
# Check vasm
vasmm68k_mot -help

# Check vbcc
vbccm68k -help

# Check vlink
vlink -help

# Check mtools
mcopy --version

# Check FS-UAE
fs-uae --version  # Linux
/Applications/FS-UAE.app/Contents/MacOS/fs-uae --version  # macOS

# Check Python
python3 --version
```

---

## Building the Project

Once all tools are installed:

```bash
cd /path/to/mariposa-os

# Build everything
make

# Or build individually
make rom      # Build ROM only
make kernel   # Build kernel only
make deploy   # Build kernel and deploy to hard drive image
make run      # Build all, deploy, and run in FS-UAE
```

---

## Troubleshooting

### "command not found" errors

Make sure `~/.local/bin` is in your PATH:

```bash
echo $PATH | grep ".local/bin"
```

If not found, add it to your shell rc file and reload.

### VBCC compilation errors

Make sure the VBCC environment variable is set:

```bash
echo $VBCC
```

Should output: `/home/youruser/.vbcc` or `/Users/youruser/.vbcc`

### FS-UAE not found on Linux

If installed via package manager, verify location:

```bash
which fs-uae
```

The Makefile will automatically detect the correct path.

### Build fails with "vasmm68k_mot: No such file"

The vasmm68k_mot variant is required (not vasmm68k_std). Make sure you built with `SYNTAX=mot`.

### mtools errors during deployment

Make sure mtools is properly installed:

```bash
mtools --version
```

The hard drive image `harddrives/boot.hdf` must be a valid FAT16 filesystem.

---

## Platform-Specific Notes

### Linux
- FS-UAE binary is typically installed as `fs-uae` in `/usr/bin` or `/usr/local/bin`
- The Makefile automatically detects this

### macOS
- FS-UAE is typically installed as `/Applications/FS-UAE.app/Contents/MacOS/fs-uae`
- The Makefile automatically detects this

### Cross-Platform Development
The project now supports automatic platform detection. The root Makefile will automatically use the correct FS-UAE path based on your operating system.
