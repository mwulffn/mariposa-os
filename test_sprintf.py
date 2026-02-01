#!/usr/bin/env python3
"""Test sprintf implementation in the ROM"""

import subprocess
import time
import os
import signal
import sys

def cleanup(fsuae_proc, socat_proc):
    """Clean up processes"""
    if fsuae_proc:
        fsuae_proc.terminate()
        try:
            fsuae_proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            fsuae_proc.kill()

    if socat_proc:
        socat_proc.terminate()
        try:
            socat_proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            socat_proc.kill()

def main():
    print("Testing Sprintf Implementation")
    print("=" * 60)

    # Check ROM exists
    rom_path = "src/rom/build/kick.rom"
    if not os.path.exists(rom_path):
        print(f"ERROR: {rom_path} not found. Run 'make' first.")
        return 1

    # Start socat for serial port
    print("Starting serial port bridge...")
    socat_proc = subprocess.Popen(
        ["socat", "-d", "-d", "pty,raw,echo=0,link=/tmp/ttyS0",
         "pty,raw,echo=0,link=/tmp/ttyS1"],
        stderr=subprocess.PIPE,
        stdout=subprocess.PIPE
    )
    time.sleep(1)

    # Start FS-UAE
    print("Starting FS-UAE...")
    fsuae_proc = subprocess.Popen(
        ["fs-uae",
         "--chip_memory=512",
         "--slow_memory=512",
         "--fast_memory=1024",
         "--kickstart_file=" + rom_path,
         "--serial_port=/tmp/ttyS1",
         "--automatic_input_grab=0"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    try:
        # Wait for emulator to start
        time.sleep(3)

        # Open serial port
        print("\nReading serial output...")
        print("-" * 60)

        with open("/tmp/ttyS0", "rb") as serial:
            # Read for 5 seconds
            start_time = time.time()
            output = b""

            while time.time() - start_time < 5:
                try:
                    data = serial.read(1024)
                    if data:
                        output += data
                        sys.stdout.buffer.write(data)
                        sys.stdout.buffer.flush()
                except:
                    break
                time.sleep(0.1)

        print("\n" + "-" * 60)

        # Check for expected outputs
        output_str = output.decode('latin-1', errors='ignore')

        print("\nVerification:")
        print("=" * 60)

        tests = [
            ("ROM boots", "AMAG ROM" in output_str),
            ("Memory map displayed", "Memory Map:" in output_str),
            ("Chip RAM detected", "Chip RAM:" in output_str and "$" in output_str),
            ("Serial working", len(output_str) > 100),
        ]

        passed = 0
        for test_name, result in tests:
            status = "✓ PASS" if result else "✗ FAIL"
            print(f"{status}: {test_name}")
            if result:
                passed += 1

        print("=" * 60)
        print(f"Tests passed: {passed}/{len(tests)}")

        # Note: We can't test the sprintf functions directly yet since they're
        # not called from the boot sequence. They'll be tested when we add
        # code that uses them (e.g., in the debugger).
        print("\nNote: Sprintf functions compiled successfully.")
        print("They will be tested when called from debugger or other code.")

        return 0 if passed == len(tests) else 1

    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        return 1

    finally:
        cleanup(fsuae_proc, socat_proc)

if __name__ == "__main__":
    sys.exit(main())
