#!/bin/bash
#
# Install vbcc, vlink, vasm for m68k bare-metal development on macOS
# Requires: Xcode command line tools (xcode-select --install)
#

set -e

PREFIX="${PREFIX:-$HOME/m68k-toolchain}"
WORKDIR=$(mktemp -d)

echo "Installing to: $PREFIX"
echo "Build directory: $WORKDIR"
mkdir -p "$PREFIX/bin"

cd "$WORKDIR"

# vlink
echo "=== Building vlink ==="
curl -O http://phoenix.owl.de/tags/vlink0_17a.tar.gz
tar xzf vlink0_17a.tar.gz
cd vlink
make
cp vlink "$PREFIX/bin/"
cd ..

# vasm (motorola syntax for m68k)
echo "=== Building vasm ==="
curl -O http://phoenix.owl.de/tags/vasm1_9a.tar.gz
tar xzf vasm1_9a.tar.gz
cd vasm
make CPU=m68k SYNTAX=mot
cp vasmm68k_mot "$PREFIX/bin/"
cd ..

# vbcc
echo "=== Building vbcc ==="
curl -O http://phoenix.owl.de/tags/vbcc0_9h.tar.gz
tar xzf vbcc0_9h.tar.gz
cd vbcc
mkdir -p bin

# Create answer file for non-interactive build
cat > answers.txt << 'EOF'
y
y
signed char
y
unsigned char
n
y
signed short
n
y
unsigned short
n
y
signed int
n
y
unsigned int
n
y
signed long
n
y
unsigned long
n
y
float
n
y
double
EOF

make TARGET=m68k < answers.txt
cp bin/vbccm68k "$PREFIX/bin/"
cp bin/vc "$PREFIX/bin/"
cd ..

# Cleanup
rm -rf "$WORKDIR"

echo ""
echo "=== Installation complete ==="
echo ""
echo "Add to your shell profile:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo ""
echo "Installed:"
echo "  vbccm68k  - C compiler for 68000"
echo "  vc        - compiler driver (optional)"
echo "  vlink     - linker"
echo "  vasmm68k_mot - assembler (motorola syntax)"
echo ""
echo "Test with:"
echo "  vbccm68k -h"
echo "  vlink -h"
echo "  vasmm68k_mot -h"
