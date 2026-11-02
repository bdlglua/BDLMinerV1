#!/usr/bin/env python3
"""Linux USB-UART hardware reset via DTR/RTS; sends no serial bytes or firmware.

Use only an explicitly identified board port with RTS wired to EN and DTR to
GPIO0, as on the classic ESP32 M5 USB-UART reset circuit. This is not a web
restart, an NVS reset or a bootloader flash command.
"""
import argparse
import fcntl
import os
from pathlib import Path
import stat
import struct
import termios
import time


def reset(port):
    path = Path(port).resolve(strict=True)
    if (path.parent != Path('/dev') or not path.name.startswith(('ttyUSB', 'ttyACM')) or
            not stat.S_ISCHR(path.stat().st_mode)):
        raise ValueError('Select the identified USB-UART character device for the M5')
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        if not os.isatty(fd):
            raise ValueError('Selected device is not a serial terminal')
        # Same D0/R1/wait/R0 pin sequence as Espressif's UART hard reset.
        # DTR deasserted keeps GPIO0 high: boot the application, not ROM download.
        fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_DTR))
        try:
            fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_RTS))
            time.sleep(0.1)
        finally:
            # Release EN even after Ctrl+C or an interrupted wait.
            fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_RTS))
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='Identified M5 /dev/serial/by-id/... or /dev/ttyUSB... port')
    args = parser.parse_args()
    try:
        reset(args.port)
    except (OSError, ValueError) as error:
        # Do not expose arbitrary device/driver details in a shared report.
        print('Hardware reset failed: ' + type(error).__name__)
        return 1
    print('Hardware reset pulse sent. Verify the result with the OTA observer.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
