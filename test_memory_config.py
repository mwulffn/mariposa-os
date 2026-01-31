#!/usr/bin/env python3
"""
Test script to verify memory configuration using the debugger.
Tests that Zorro II autoconfig and memory detection work correctly.
"""

import socket
import subprocess
import sys
import time
import os
import signal

class DebuggerTest:
    def __init__(self):
        self.emulator_proc = None
        self.sock = None
        self.test_count = 0
        self.pass_count = 0
        self.fail_count = 0

    def start_emulator(self):
        """Start FS-UAE emulator."""
        print("Starting FS-UAE...")
        # Start FS-UAE via make run
        self.emulator_proc = subprocess.Popen(
            ['make', 'run'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid  # Create new process group for clean shutdown
        )
        # Give it time to start
        time.sleep(4)

    def connect_debugger(self):
        """Connect to the debugger serial port."""
        print("Connecting to debugger...", end='', flush=True)
        max_retries = 10
        for i in range(max_retries):
            try:
                # Create new socket for each attempt
                if self.sock:
                    try:
                        self.sock.close()
                    except:
                        pass
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(('localhost', 5555))
                print(" Connected!")
                # Wait for prompt
                time.sleep(1)
                # Clear initial output
                self.sock.settimeout(0.5)
                try:
                    while True:
                        self.sock.recv(4096)
                except socket.timeout:
                    pass
                self.sock.settimeout(5.0)
                return True
            except (ConnectionRefusedError, OSError) as e:
                if i < max_retries - 1:
                    print('.', end='', flush=True)
                    time.sleep(1)
                else:
                    print(f" Failed! Error: {e}")
                    return False
        return False

    def send_command(self, cmd):
        """Send a command and get response."""
        self.sock.sendall((cmd + '\n').encode())
        time.sleep(0.3)
        response = b''
        self.sock.settimeout(1.0)
        try:
            while True:
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
                response += chunk
        except socket.timeout:
            pass
        self.sock.settimeout(5.0)
        return response.decode('ascii', errors='replace')

    def test_memory_var(self, name, address, expected_value):
        """Test a memory variable has the expected value."""
        self.test_count += 1
        print(f"\nTest {self.test_count}: {name} at ${address:08X}")

        cmd = f"m.l ${address:X}"
        response = self.send_command(cmd)
        print(f"  Command: {cmd}")
        print(f"  Response: {response.strip()}")

        # Extract hex value from response (format: $XXXXXXXX: $YYYYYYYY)
        try:
            # Look for pattern like "$XXXXXXXX: $YYYYYYYY"
            if ':' in response:
                value_str = response.split(':')[1].strip().split()[0]
                # Remove $ if present
                value_str = value_str.replace('$', '')
                actual_value = int(value_str, 16)

                if actual_value == expected_value:
                    print(f"  ✓ PASS: {name} = ${actual_value:08X}")
                    self.pass_count += 1
                    return True
                else:
                    print(f"  ✗ FAIL: Expected ${expected_value:08X}, got ${actual_value:08X}")
                    self.fail_count += 1
                    return False
            else:
                print(f"  ✗ FAIL: Could not parse response")
                self.fail_count += 1
                return False
        except (ValueError, IndexError) as e:
            print(f"  ✗ FAIL: Error parsing response: {e}")
            self.fail_count += 1
            return False

    def test_memory_write(self, address, test_value):
        """Test writing and reading back from memory."""
        self.test_count += 1
        print(f"\nTest {self.test_count}: Write/Read test at ${address:08X}")

        # Write value
        cmd = f"m ${address:X} {test_value:X}"
        response = self.send_command(cmd)
        print(f"  Write: {cmd}")

        # Read back
        cmd = f"m.l ${address:X}"
        response = self.send_command(cmd)
        print(f"  Read: {cmd}")
        print(f"  Response: {response.strip()}")

        try:
            if ':' in response:
                value_str = response.split(':')[1].strip().split()[0]
                value_str = value_str.replace('$', '')
                actual_value = int(value_str, 16)

                if actual_value == test_value:
                    print(f"  ✓ PASS: Write persisted (${actual_value:08X})")
                    self.pass_count += 1
                    return True
                else:
                    print(f"  ✗ FAIL: Expected ${test_value:08X}, got ${actual_value:08X}")
                    self.fail_count += 1
                    return False
            else:
                print(f"  ✗ FAIL: Could not parse response")
                self.fail_count += 1
                return False
        except (ValueError, IndexError) as e:
            print(f"  ✗ FAIL: Error parsing response: {e}")
            self.fail_count += 1
            return False

    def test_memory_map(self):
        """Test reading the memory map."""
        self.test_count += 1
        print(f"\nTest {self.test_count}: Memory map")

        cmd = "m $3250"
        response = self.send_command(cmd)
        print(f"  Command: {cmd}")
        print(f"  Response:")
        for line in response.split('\n')[:10]:
            if line.strip():
                print(f"    {line}")

        # Just check we got some response
        if len(response) > 50:
            print(f"  ✓ PASS: Memory map retrieved")
            self.pass_count += 1
            return True
        else:
            print(f"  ✗ FAIL: Memory map too short")
            self.fail_count += 1
            return False

    def cleanup(self):
        """Clean up resources."""
        print("\nCleaning up...")
        if self.sock:
            try:
                self.send_command('q')
            except:
                pass
            self.sock.close()
        if self.emulator_proc:
            self.emulator_proc.terminate()
            try:
                self.emulator_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.emulator_proc.kill()

    def run_tests(self):
        """Run all memory configuration tests."""
        print("=" * 60)
        print("MEMORY CONFIGURATION TEST")
        print("=" * 60)

        try:
            self.start_emulator()
            if not self.connect_debugger():
                print("Failed to connect to debugger")
                return False

            print("\n" + "=" * 60)
            print("TESTING MEMORY VARIABLES")
            print("=" * 60)

            # Test chip RAM (should be 1MB = $100000)
            self.test_memory_var("CHIP_RAM_VAR", 0x460, 0x100000)

            # Test slow RAM (should be 0)
            self.test_memory_var("SLOW_RAM_VAR", 0x464, 0x000000)

            # Test fast RAM size (should be 8MB = $800000)
            self.test_memory_var("FAST_RAM_VAR", 0x468, 0x800000)

            # Test fast RAM base (should be $200000 after autoconfig)
            self.test_memory_var("FAST_RAM_BASE", 0x46C, 0x200000)

            print("\n" + "=" * 60)
            print("TESTING FAST RAM ACCESS")
            print("=" * 60)

            # Test writing to fast RAM base
            self.test_memory_write(0x200000, 0xDEADBEEF)

            # Test writing to fast RAM at +1MB
            self.test_memory_write(0x300000, 0xCAFEBABE)

            # Test writing to fast RAM at +7MB (near end)
            self.test_memory_write(0x900000, 0x12345678)

            print("\n" + "=" * 60)
            print("TESTING MEMORY MAP")
            print("=" * 60)

            self.test_memory_map()

            print("\n" + "=" * 60)
            print("TEST SUMMARY")
            print("=" * 60)
            print(f"Total tests:  {self.test_count}")
            print(f"Passed:       {self.pass_count}")
            print(f"Failed:       {self.fail_count}")

            if self.fail_count == 0:
                print("\n✓ ALL TESTS PASSED")
                return True
            else:
                print(f"\n✗ {self.fail_count} TEST(S) FAILED")
                return False

        finally:
            self.cleanup()

def main():
    tester = DebuggerTest()
    success = tester.run_tests()
    return 0 if success else 1

if __name__ == '__main__':
    sys.exit(main())
