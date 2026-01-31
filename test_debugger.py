#!/usr/bin/env python3
"""Test the interactive debugger by sending commands via serial"""

import socket
import time
import sys
import subprocess

def send_command(sock, cmd, delay=0.5):
    """Send a command and wait for response"""
    print(f"\n>>> Sending: {cmd}")
    sock.sendall(cmd.encode() + b'\n')
    time.sleep(delay)

    # Read available data
    sock.settimeout(0.5)
    try:
        data = sock.recv(4096)
        if data:
            print(data.decode('ascii', errors='replace'), end='')
    except socket.timeout:
        pass
    sock.settimeout(None)

def main():
    print("=== Debugger Interactive Test ===\n")

    # Start FS-UAE
    print("1. Starting FS-UAE...")
    fsuae = subprocess.Popen(['make', 'run'],
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)

    time.sleep(3)

    try:
        # Connect to serial port
        print("2. Connecting to serial port...")
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

        # Test commands
        print("\n3. Testing commands...\n")

        send_command(sock, '?', 1)
        send_command(sock, 'r', 1)
        send_command(sock, 'r D0 DEADBEEF', 1)
        send_command(sock, 'r D0 12345678', 1)
        send_command(sock, 'm 0', 1)
        send_command(sock, 'm', 1)
        send_command(sock, 'm FC0000', 1)

        sock.close()

    finally:
        print("\n\n4. Stopping FS-UAE...")
        fsuae.terminate()
        fsuae.wait()

    print("\n=== Test Complete ===")

if __name__ == '__main__':
    main()
