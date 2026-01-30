#!/bin/bash
# Test script for Amiga serial port debugging
# Usage: ./test_serial.sh

set -e

OUTPUT_FILE="/tmp/amiga_serial.txt"
rm -f "$OUTPUT_FILE"

echo "=== Amiga Serial Port Test ==="
echo ""
echo "1. Starting FS-UAE (waiting for serial connection)..."
make run > /dev/null 2>&1 &

# Wait for FS-UAE to start
sleep 5

echo "2. Connecting serial reader..."
/opt/homebrew/bin/python3 serial_reader.py > "$OUTPUT_FILE" 2>&1 &
READER_PID=$!

# Let it run for 8 seconds to capture boot messages
sleep 8

echo "3. Stopping FS-UAE..."
pkill -9 -f "FS-UAE" 2>/dev/null || true
sleep 1
kill -9 $READER_PID 2>/dev/null || true

echo "4. Results:"
echo ""
cat "$OUTPUT_FILE"

echo ""
echo "=== Test Complete ==="
