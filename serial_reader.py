#!/usr/bin/env python3
"""Serial port reader for FS-UAE debugging"""

import socket
import sys
import time

def read_serial(host='localhost', port=5555, timeout=5):
    """Connect to FS-UAE serial port and read data"""
    try:
        print(f"Connecting to {host}:{port}...", file=sys.stderr)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        sock.connect((host, port))
        print("Connected!", file=sys.stderr)

        sock.settimeout(timeout)
        data = b''
        start_time = time.time()

        while time.time() - start_time < timeout:
            try:
                chunk = sock.recv(1024)
                if not chunk:
                    break
                data += chunk
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
            except socket.timeout:
                # Keep trying until overall timeout
                if time.time() - start_time < timeout:
                    continue
                break
            except Exception as e:
                print(f"\nRead error: {e}", file=sys.stderr)
                break

        sock.close()
        print(f"\n\nTotal bytes received: {len(data)}", file=sys.stderr)
        return data

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return b''

if __name__ == '__main__':
    read_serial()
