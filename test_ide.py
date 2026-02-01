#!/usr/bin/env python3
"""
Test script for IDE sector read functionality.
Launches FS-UAE with A600 config and connects to serial port to verify output.
"""

import subprocess
import time
import socket
import sys
import os
import signal

def run_test():
    """Run the IDE test and capture serial output."""

    # Start FS-UAE in the background
    print("Starting FS-UAE with A600 configuration...")
    config_path = os.path.join(os.getcwd(), "configs/a600.fs-uae")
    emulator = subprocess.Popen(
        ["/Applications/FS-UAE.app/Contents/MacOS/fs-uae", config_path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    # Give emulator time to start
    time.sleep(2)

    try:
        # Connect to serial port
        print("Connecting to serial port (localhost:5555)...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Try to connect with retries
        for i in range(10):
            try:
                sock.connect(('localhost', 5555))
                print("Connected to serial port!")
                break
            except ConnectionRefusedError:
                if i == 9:
                    raise
                time.sleep(1)

        # Read serial output for up to 10 seconds
        print("\n--- Serial Output ---")
        sock.settimeout(1.0)
        start_time = time.time()
        output = b""

        while time.time() - start_time < 10:
            try:
                data = sock.recv(1024)
                if data:
                    output += data
                    print(data.decode('ascii', errors='replace'), end='', flush=True)

                    # Check if we got the IDE test output
                    if b"IDE:" in output:
                        # Give it a bit more time to finish
                        time.sleep(1)
                        break
            except socket.timeout:
                continue

        print("\n--- End Serial Output ---\n")

        # Analyze output
        output_str = output.decode('ascii', errors='replace')

        if "IDE: FAT16 signature valid!" in output_str:
            print("✓ SUCCESS: FAT16 signature verified!")
            return True
        elif "IDE: Invalid signature:" in output_str:
            print("✗ FAIL: Invalid FAT16 signature")
            return False
        elif "IDE: Timeout" in output_str:
            print("✗ FAIL: IDE timeout (no drive or drive not responding)")
            return False
        elif "IDE:" in output_str:
            print("? PARTIAL: IDE messages received but no clear result")
            return False
        else:
            print("✗ FAIL: No IDE messages in output")
            return False

    except Exception as e:
        print(f"Error: {e}")
        return False
    finally:
        # Clean up
        try:
            sock.close()
        except:
            pass

        print("\nStopping emulator...")
        emulator.terminate()
        try:
            emulator.wait(timeout=5)
        except subprocess.TimeoutExpired:
            emulator.kill()
            emulator.wait()

if __name__ == "__main__":
    success = run_test()
    sys.exit(0 if success else 1)
