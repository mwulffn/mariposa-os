#!/bin/bash
# Test the debug.py convenience script

echo "Testing debug.py convenience script..."
echo ""

# Send commands via pipe
{
    sleep 3
    echo "?"
    sleep 1
    echo "r"
    sleep 1
    echo "m 0"
    sleep 1
    echo "quit"
} | timeout 15 python3 debug.py

echo ""
echo "Test complete!"
