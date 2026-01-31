#!/bin/bash
# Test the interactive debugger by sending commands

echo "=== Debugger Interactive Test ==="
echo ""

# Start FS-UAE in background
echo "1. Starting FS-UAE..."
make run >/dev/null 2>&1 &
FSUAE_PID=$!

# Wait for FS-UAE to start
sleep 3

echo "2. Sending commands to debugger..."
echo ""

# Connect and send commands
{
    sleep 2
    echo "Testing help command..."
    echo "?"
    sleep 1

    echo "Testing register display..."
    echo "r"
    sleep 1

    echo "Testing register modification..."
    echo "r D0 DEADBEEF"
    sleep 1
    echo "r"
    sleep 1

    echo "Testing memory dump..."
    echo "m 0"
    sleep 1
    echo "m"
    sleep 1
    echo "m FC0000"
    sleep 1

    sleep 2
} | nc localhost 5555

# Cleanup
echo ""
echo "3. Stopping FS-UAE..."
kill $FSUAE_PID 2>/dev/null
wait $FSUAE_PID 2>/dev/null

echo ""
echo "=== Test Complete ==="
