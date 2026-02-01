#!/usr/bin/env python3
"""
Interactive Amiga Debugger
Launches FS-UAE and provides interactive serial debugging session.
"""

import socket
import subprocess
import sys
import time
import threading
import select
import os
import signal
import tty
import termios

class AmigaDebugger:
    def __init__(self):
        self.fsuae_process = None
        self.sock = None
        self.running = False
        self.reader_thread = None
        self.prompt_ready = threading.Event()  # Signals when Amiga prompt is seen

    def start_emulator(self):
        """Start FS-UAE in the background"""
        print("Starting FS-UAE emulator...")
        self.fsuae_process = subprocess.Popen(
            ['make', 'run'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid  # Create new process group for clean shutdown
        )

        # Wait for emulator to start and serial port to be ready
        print("Waiting for emulator to initialize...", end='', flush=True)
        for i in range(6):
            time.sleep(0.5)
            print('.', end='', flush=True)
        print(" OK")

    def connect_serial(self):
        """Connect to the serial port"""
        print("Connecting to serial port (localhost:5555)...", end='', flush=True)
        max_attempts = 5
        for attempt in range(max_attempts):
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect(('localhost', 5555))
                print(" Connected!")
                return True
            except ConnectionRefusedError:
                if attempt < max_attempts - 1:
                    print('.', end='', flush=True)
                    time.sleep(1)
                else:
                    print(" Failed!")
                    return False
        return False

    def read_serial_output(self):
        """Thread function to continuously read and display serial output"""
        self.sock.settimeout(0.1)
        buffer = []  # Accumulate text to check for debugger banner

        while self.running:
            try:
                data = self.sock.recv(4096)
                if data:
                    text = data.decode('ascii', errors='replace')
                    # Print without newline if it doesn't end with one
                    print(text, end='', flush=True)
                    # Signal when we see the prompt after the debugger banner
                    if not self.prompt_ready.is_set():
                        buffer.append(text)
                        combined = ''.join(buffer[-10:])  # Keep last 10 chunks to limit memory
                        # Look for various prompt patterns
                        # Debug: uncomment to see what we're looking for
                        # if 'Debugger' in text:
                        #     print(f"\n[DEBUG] Received: {repr(combined[-50:])}\n", flush=True)
                        if ('> \n' in combined or combined.endswith('> ') or
                            (combined.count('>') > 0 and combined.strip().endswith('>'))):
                            self.prompt_ready.set()
                else:
                    # Empty data means connection closed (FS-UAE quit)
                    if self.running:
                        print("\n\n[ERROR] Serial connection closed (FS-UAE may have quit)")
                        self.running = False
                    break
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"\n[Serial read error: {e}]")
                    self.running = False
                break

    def read_char(self):
        """Read a single character in raw mode (no echo)"""
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            ch = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
        return ch

    def send_command(self, cmd):
        """Send a command to the debugger"""
        try:
            self.sock.sendall(cmd.encode() + b'\n')
        except (BrokenPipeError, ConnectionResetError):
            print("\n\n[ERROR] Serial connection lost (FS-UAE may have quit)")
            self.running = False
        except Exception as e:
            print(f"\n[ERROR] Failed to send command: {e}")
            self.running = False

    def interactive_session(self):
        """Run interactive debugging session"""
        # Start reader thread
        self.running = True
        self.reader_thread = threading.Thread(target=self.read_serial_output, daemon=True)
        self.reader_thread.start()

        # Wait for Amiga's initial prompt
        if not self.prompt_ready.wait(timeout=10):
            print("\n[WARNING] Amiga prompt not detected, continuing anyway...")

        try:
            while self.running:
                try:
                    # Check if reader thread died (connection lost)
                    if not self.reader_thread.is_alive():
                        print("\n[ERROR] Connection lost, exiting...")
                        break

                    # Read command from user
                    if sys.stdin.isatty():
                        # Build command character by character in raw mode
                        cmd = []
                        while True:
                            ch = self.read_char()
                            if ch in ('\r', '\n'):
                                self.sock.sendall(b'\r')  # Send CR to trigger command
                                break
                            elif ch == '\x04':  # Ctrl-D
                                raise EOFError
                            elif ch == '\x03':  # Ctrl-C
                                raise KeyboardInterrupt
                            else:
                                cmd.append(ch)
                                self.sock.sendall(ch.encode())  # Send char immediately
                        cmd = ''.join(cmd)
                    else:
                        cmd = sys.stdin.readline()
                        if not cmd:
                            break
                        cmd = cmd.rstrip('\n')

                    # Check for exit commands
                    if cmd.lower() in ['quit', 'exit', 'q']:
                        print("\nExiting debugger...")
                        break

                    # Send command (only for non-TTY mode, TTY already sent)
                    if cmd and not sys.stdin.isatty():
                        self.send_command(cmd)

                    # Small delay to let output arrive
                    time.sleep(0.1)

                except EOFError:
                    print("\nExiting debugger...")
                    break
                except KeyboardInterrupt:
                    print("\nUse 'quit' to exit or Ctrl-D")
                    continue

        finally:
            self.running = False

    def cleanup(self):
        """Clean up resources"""
        print("\nCleaning up...")

        # Stop reader thread
        self.running = False
        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=1)

        # Close socket
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
            self.sock = None

        # Stop emulator
        if self.fsuae_process:
            try:
                # Kill entire process group
                os.killpg(os.getpgid(self.fsuae_process.pid), signal.SIGTERM)
                self.fsuae_process.wait(timeout=2)
            except:
                try:
                    os.killpg(os.getpgid(self.fsuae_process.pid), signal.SIGKILL)
                except:
                    pass
            self.fsuae_process = None

        print("Done.")

    def run(self):
        """Main entry point"""
        try:
            # Start emulator
            self.start_emulator()

            # Connect to serial
            if not self.connect_serial():
                print("Failed to connect to serial port!")
                print("Make sure FS-UAE is configured correctly.")
                return 1

            # Run interactive session
            self.interactive_session()

            return 0

        except Exception as e:
            print(f"\nError: {e}")
            import traceback
            traceback.print_exc()
            return 1

        finally:
            self.cleanup()


def main():
    """Entry point"""
    # Check if ROM exists
    if not os.path.exists('build/kick.rom'):
        print("Error: ROM not found at build/kick.rom")
        print("Please run 'make' first to build the ROM.")
        return 1

    # Check if make/fs-uae are available
    if os.system('which make >/dev/null 2>&1') != 0:
        print("Error: 'make' command not found")
        return 1

    # Create and run debugger
    debugger = AmigaDebugger()
    return debugger.run()


if __name__ == '__main__':
    sys.exit(main())
