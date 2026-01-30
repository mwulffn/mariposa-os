#!/bin/bash
# Test script for serial port debugging

OUTPUT_FILE="/tmp/amiga_serial_output.txt"
rm -f "$OUTPUT_FILE"

echo "Starting FS-UAE (will wait for serial connection)..."
make run &
MAKE_PID=$!

# Wait for FS-UAE process to actually start
sleep 3
until pgrep -f "FS-UAE" > /dev/null; do
    echo "Waiting for FS-UAE to start..."
    sleep 1
done

echo "FS-UAE is running, connecting netcat..."

# Connect with nc and capture output for limited time
# We'll run nc in background and kill it after capturing data
(nc localhost 5555 > "$OUTPUT_FILE") &
NC_PID=$!

# Wait for boot and serial output
echo "Waiting for serial output (10 seconds)..."
sleep 10

# Kill FS-UAE first
echo "Closing FS-UAE..."
pkill -9 -f "FS-UAE" 2>/dev/null

# Give it a moment then kill netcat
sleep 1
kill -9 $NC_PID 2>/dev/null

# Show results
echo ""
echo "=== Serial Output Captured ==="
if [ -f "$OUTPUT_FILE" ]; then
    cat "$OUTPUT_FILE"
    echo ""
    echo "=== File Size: $(wc -c < "$OUTPUT_FILE") bytes ==="
else
    echo "ERROR: No output file created"
fi
