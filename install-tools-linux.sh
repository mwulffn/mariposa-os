#!/bin/bash
# Installation script for Amiga OS build tools on Linux
# Run with: bash install-tools-linux.sh

set -e  # Exit on error

echo "=========================================="
echo "Amiga OS Build Tools Installation (Linux)"
echo "=========================================="
echo ""

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running with sufficient privileges for apt
if ! command -v sudo &> /dev/null; then
    echo -e "${YELLOW}Warning: sudo not found. You may need root access for some steps.${NC}"
fi

# Install directory
INSTALL_DIR="$HOME/.local/bin"
VBCC_DIR="$HOME/.vbcc"

# Create install directory
mkdir -p "$INSTALL_DIR"
mkdir -p "$VBCC_DIR"

echo ""
echo "Installation directory: $INSTALL_DIR"
echo "VBCC directory: $VBCC_DIR"
echo ""

# Step 1: Install system packages
echo -e "${GREEN}[1/5] Installing system packages...${NC}"
sudo apt update
sudo apt install -y build-essential curl wget mtools

# Step 2: Install VASM
echo ""
echo -e "${GREEN}[2/5] Building and installing VASM...${NC}"
cd /tmp
if [ -d "vasm" ]; then
    rm -rf vasm
fi
wget -q http://sun.hasenbraten.de/vasm/release/vasm.tar.gz
tar -xzf vasm.tar.gz
cd vasm
make CPU=m68k SYNTAX=mot
cp vasmm68k_mot "$INSTALL_DIR/"
cp vobjdump "$INSTALL_DIR/"
cd /tmp
rm -rf vasm vasm.tar.gz
echo "VASM installed to $INSTALL_DIR/vasmm68k_mot"

# Step 3: Install VBCC
echo ""
echo -e "${GREEN}[3/5] Building and installing VBCC...${NC}"
cd /tmp
if [ -d "vbcc" ]; then
    rm -rf vbcc
fi
wget -q http://www.ibaug.de/vbcc/vbcc.tar.gz
tar -xzf vbcc.tar.gz
cd vbcc
mkdir -p bin
make TARGET=m68k
cp bin/vbccm68k "$INSTALL_DIR/"
cd /tmp
rm -rf vbcc vbcc.tar.gz
echo "VBCC installed to $INSTALL_DIR/vbccm68k"

# Step 4: Install VLINK
echo ""
echo -e "${GREEN}[4/5] Building and installing VLINK...${NC}"
cd /tmp
if [ -d "vlink" ]; then
    rm -rf vlink
fi
wget -q http://sun.hasenbraten.de/vlink/release/vlink.tar.gz
tar -xzf vlink.tar.gz
cd vlink
make
cp vlink "$INSTALL_DIR/"
cd /tmp
rm -rf vlink vlink.tar.gz
echo "VLINK installed to $INSTALL_DIR/vlink"

# Step 5: Install VBCC target libraries
# echo ""
# echo -e "${GREEN}[5/5] Installing VBCC target libraries...${NC}"
# cd /tmp
# wget -q http://www.ibaug.de/vbcc/vbcc_target_m68k-amigaos.tar.gz
# tar -xzf vbcc_target_m68k-amigaos.tar.gz -C "$VBCC_DIR"
# rm -f vbcc_target_m68k-amigaos.tar.gz
# echo "VBCC targets installed to $VBCC_DIR"

# Step 6: Check PATH configuration
echo ""
echo -e "${GREEN}Installation complete!${NC}"
echo ""
echo "=========================================="
echo "IMPORTANT: Configure your shell"
echo "=========================================="
echo ""
echo "Add these lines to your ~/.bashrc or ~/.zshrc:"
echo ""
echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
echo "    export VBCC=\"\$HOME/.vbcc\""
echo ""
echo "Then reload your shell:"
echo ""
echo "    source ~/.bashrc  # or source ~/.zshrc"
echo ""

# Verify installations
echo "=========================================="
echo "Verifying installations..."
echo "=========================================="
echo ""

FAIL=0

if [ -x "$INSTALL_DIR/vasmm68k_mot" ]; then
    echo -e "${GREEN}✓ vasmm68k_mot installed${NC}"
else
    echo -e "${YELLOW}✗ vasmm68k_mot not found${NC}"
    FAIL=1
fi

if [ -x "$INSTALL_DIR/vbccm68k" ]; then
    echo -e "${GREEN}✓ vbccm68k installed${NC}"
else
    echo -e "${YELLOW}✗ vbccm68k not found${NC}"
    FAIL=1
fi

if [ -x "$INSTALL_DIR/vlink" ]; then
    echo -e "${GREEN}✓ vlink installed${NC}"
else
    echo -e "${YELLOW}✗ vlink not found${NC}"
    FAIL=1
fi

if command -v mcopy &> /dev/null; then
    echo -e "${GREEN}✓ mtools installed${NC}"
else
    echo -e "${YELLOW}✗ mtools not found${NC}"
    FAIL=1
fi

if command -v fs-uae &> /dev/null; then
    echo -e "${GREEN}✓ fs-uae installed${NC}"
else
    echo -e "${YELLOW}✗ fs-uae not found (optional for running)${NC}"
fi

echo ""
if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}All tools installed successfully!${NC}"
    echo ""
    echo "After configuring your PATH, run: make"
else
    echo -e "${YELLOW}Some tools failed to install. Check errors above.${NC}"
    exit 1
fi
