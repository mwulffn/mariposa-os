#!/usr/bin/env python3
"""Test the debug.py script by sending commands via subprocess"""

import subprocess
import time
import sys

def test_debug_script():
    print("=" * 60)
    print("Testing debug.py convenience script")
    print("=" * 60)
    print()

    # Start debug.py
    proc = subprocess.Popen(
        ['python3', 'debug.py'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    try:
        # Wait for startup
        print("Waiting for debugger to start...")
        time.sleep(5)

        # Send commands
        commands = ['?', 'r', 'm 0', 'quit']

        for cmd in commands:
            print(f"\nSending command: {cmd}")
            proc.stdin.write(cmd + '\n')
            proc.stdin.flush()
            time.sleep(1.5)

        # Wait a bit for output
        time.sleep(2)

        # Terminate
        proc.stdin.close()
        proc.wait(timeout=5)

        print("\n" + "=" * 60)
        print("Output from debug.py:")
        print("=" * 60)
        print(proc.stdout.read())

        print("\n✓ Test completed successfully")
        return 0

    except Exception as e:
        print(f"\n✗ Test failed: {e}")
        proc.kill()
        proc.wait()
        return 1

if __name__ == '__main__':
    sys.exit(test_debug_script())
