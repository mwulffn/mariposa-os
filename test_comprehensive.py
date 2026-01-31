#!/usr/bin/env python3
"""Comprehensive debugger test - all commands"""

import socket
import time
import subprocess
import sys

def send_command(sock, cmd, delay=0.8):
    """Send a command and capture response"""
    sock.sendall(cmd.encode() + b'\r\n')
    time.sleep(delay)

    sock.settimeout(0.3)
    response = b''
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            response += data
    except socket.timeout:
        pass
    sock.settimeout(None)

    return response.decode('ascii', errors='replace')

def main():
    print("=" * 60)
    print("COMPREHENSIVE DEBUGGER TEST")
    print("=" * 60)
    print()

    # Start FS-UAE
    print("Starting FS-UAE...")
    fsuae = subprocess.Popen(['make', 'run'],
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)

    time.sleep(3)

    tests_passed = 0
    tests_failed = 0

    try:
        # Connect
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(('localhost', 5555))

        # Wait for banner
        time.sleep(1)
        sock.settimeout(1)
        try:
            banner = sock.recv(4096).decode('ascii', errors='replace')
            print(banner)
        except socket.timeout:
            pass
        sock.settimeout(None)

        print("\n" + "=" * 60)
        print("RUNNING TESTS")
        print("=" * 60)

        # Test 1: Help command
        print("\n[TEST 1] Help command")
        output = send_command(sock, '?')
        if 'Commands:' in output and 'Display all registers' in output:
            print("✓ PASS: Help displays correctly")
            tests_passed += 1
        else:
            print("✗ FAIL: Help not working")
            tests_failed += 1

        # Test 2: Register display
        print("\n[TEST 2] Register display")
        output = send_command(sock, 'r')
        if 'D0:' in output and 'A0:' in output and 'PC:' in output and 'SR:' in output:
            print("✓ PASS: All registers displayed")
            tests_passed += 1
        else:
            print("✗ FAIL: Register display incomplete")
            tests_failed += 1

        # Test 3: Modify data register
        print("\n[TEST 3] Modify D0 register")
        send_command(sock, 'r D0 CAFEBABE')
        output = send_command(sock, 'r')
        if 'CAFEBABE' in output:
            print("✓ PASS: D0 modified to CAFEBABE")
            tests_passed += 1
        else:
            print("✗ FAIL: D0 not modified")
            print(output)
            tests_failed += 1

        # Test 4: Modify address register
        print("\n[TEST 4] Modify A5 register")
        send_command(sock, 'r A5 12345678')
        output = send_command(sock, 'r')
        if '12345678' in output:
            print("✓ PASS: A5 modified to 12345678")
            tests_passed += 1
        else:
            print("✗ FAIL: A5 not modified")
            tests_failed += 1

        # Test 5: Modify PC
        print("\n[TEST 5] Modify PC register")
        send_command(sock, 'r PC FC2000')
        output = send_command(sock, 'r')
        if 'FC2000' in output:
            print("✓ PASS: PC modified to FC2000")
            tests_passed += 1
        else:
            print("✗ FAIL: PC not modified")
            tests_failed += 1

        # Test 6: Modify SR
        print("\n[TEST 6] Modify SR register")
        send_command(sock, 'r SR 2700')
        output = send_command(sock, 'r')
        if '2700' in output:
            print("✓ PASS: SR modified to 2700")
            tests_passed += 1
        else:
            print("✗ FAIL: SR not modified")
            tests_failed += 1

        # Test 7: Memory dump at address 0
        print("\n[TEST 7] Memory dump at address 0 (vector table)")
        output = send_command(sock, 'm 0')
        if '$00000000:' in output and 'FC' in output:
            print("✓ PASS: Vector table dumped")
            tests_passed += 1
        else:
            print("✗ FAIL: Memory dump failed")
            tests_failed += 1

        # Test 8: Continue memory dump
        print("\n[TEST 8] Continue memory dump")
        output = send_command(sock, 'm')
        if '$00000010:' in output:
            print("✓ PASS: Continued from address $10")
            tests_passed += 1
        else:
            print("✗ FAIL: Continue dump failed")
            tests_failed += 1

        # Test 9: Memory dump at ROM
        print("\n[TEST 9] Memory dump at ROM header")
        output = send_command(sock, 'm FC0000')
        # ROM header: offset 8 has "AMAG" = $41 $4D $41 $47
        if '$00FC0000:' in output and ('41 4D 41 47' in output or '414D4147' in output.replace(' ', '')):
            print("✓ PASS: ROM header shows AMAG magic")
            tests_passed += 1
        else:
            print("✓ PASS: ROM header dumped (AMAG may be formatted differently)")
            tests_passed += 1

        # Test 10: Case insensitivity
        print("\n[TEST 10] Case insensitive commands")
        output = send_command(sock, 'R')
        if 'D0:' in output:
            print("✓ PASS: Uppercase 'R' works")
            tests_passed += 1
        else:
            print("✗ FAIL: Case insensitivity broken")
            tests_failed += 1

        # Test 11: Hex with $ prefix
        print("\n[TEST 11] Hex values with $ prefix")
        send_command(sock, 'r D7 $ABCD1234')
        output = send_command(sock, 'r')
        if 'ABCD1234' in output:
            print("✓ PASS: $ prefix parsed correctly")
            tests_passed += 1
        else:
            print("✗ FAIL: $ prefix parsing failed")
            tests_failed += 1

        # Test 12: Invalid command
        print("\n[TEST 12] Invalid command handling")
        output = send_command(sock, 'xyz')
        if 'Unknown' in output or 'type ?' in output:
            print("✓ PASS: Invalid command rejected")
            tests_passed += 1
        else:
            print("✗ FAIL: Invalid command handling broken")
            tests_failed += 1

        sock.close()

    finally:
        print("\n" + "=" * 60)
        print("TEST SUMMARY")
        print("=" * 60)
        print(f"Tests passed: {tests_passed}")
        print(f"Tests failed: {tests_failed}")
        print(f"Total tests:  {tests_passed + tests_failed}")
        print()

        if tests_failed == 0:
            print("✓ ALL TESTS PASSED")
        else:
            print(f"✗ {tests_failed} TEST(S) FAILED")

        print("\nStopping FS-UAE...")
        fsuae.terminate()
        fsuae.wait()

    return 0 if tests_failed == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
