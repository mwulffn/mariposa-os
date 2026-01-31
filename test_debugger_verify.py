#!/usr/bin/env python3
"""Verify debugger register modification works"""

import socket
import time
import subprocess

def send_command(sock, cmd, delay=0.5):
    """Send a command and wait for response"""
    print(f">>> {cmd}")
    sock.sendall(cmd.encode() + b'\n')
    time.sleep(delay)

    # Read available data
    sock.settimeout(0.5)
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

    output = response.decode('ascii', errors='replace')
    print(output)
    return output

def main():
    print("=== Verify Register Modification ===\n")

    # Start FS-UAE
    print("Starting FS-UAE...")
    fsuae = subprocess.Popen(['make', 'run'],
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)

    time.sleep(3)

    try:
        # Connect
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(('localhost', 5555))

        # Wait for banner
        time.sleep(1)
        sock.settimeout(1)
        try:
            data = sock.recv(4096)
            print(data.decode('ascii', errors='replace'))
        except socket.timeout:
            pass
        sock.settimeout(None)

        # Test sequence
        print("\n=== Test Sequence ===\n")

        # Display initial registers
        print("1. Initial register state:")
        send_command(sock, 'r', 1)

        # Modify D0
        print("\n2. Setting D0 to DEADBEEF:")
        send_command(sock, 'r D0 DEADBEEF', 1)

        # Verify D0 changed
        print("\n3. Verify D0 changed:")
        output = send_command(sock, 'r', 1)

        if 'DEADBEEF' in output:
            print("\n✓ SUCCESS: D0 was modified to DEADBEEF")
        else:
            print("\n✗ FAIL: D0 was not modified")

        # Modify PC
        print("\n4. Setting PC to FC1000:")
        send_command(sock, 'r PC FC1000', 1)

        # Verify PC
        print("\n5. Verify PC changed:")
        output = send_command(sock, 'r', 1)

        if 'FC1000' in output or 'FC 10 00' in output:
            print("\n✓ SUCCESS: PC was modified")
        else:
            print("\n✗ FAIL: PC was not modified")

        sock.close()

    finally:
        print("\nStopping FS-UAE...")
        fsuae.terminate()
        fsuae.wait()

    print("\n=== Test Complete ===")

if __name__ == '__main__':
    main()
