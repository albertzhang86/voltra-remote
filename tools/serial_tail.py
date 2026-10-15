#!/usr/bin/env python3
"""Read serial output for a bounded duration without intentionally resetting the board.

Run with: uv run --with pyserial tools/serial_tail.py --seconds 10
Host drivers may still toggle control lines when opening a port. Start diagnostics
only while the connected training equipment is unloaded.
"""

import argparse
import codecs
import contextlib
import glob
import sys
import time


def disable_hangup_on_close(port):
    """Best effort: retain control-line state when the POSIX descriptor closes."""
    try:
        import termios
    except ImportError:
        return False
    try:
        attributes = termios.tcgetattr(port.fileno())
        attributes[2] &= ~termios.HUPCL
        termios.tcsetattr(port.fileno(), termios.TCSANOW, attributes)
        return True
    except (AttributeError, OSError, termios.error):
        return False


def open_monitor(device, baud, serial_factory):
    """Match Espressif's no-reset open sequence; never deassert DTR first.

    https://github.com/espressif/esp-idf-monitor/blob/master/esp_idf_monitor/base/serial_reader.py
    pySerial's open() writes DTR before RTS. Starting both asserted avoids the
    transient RTS=True/DTR=False state that resets native ESP32-S3 USB-JTAG.
    """
    port = serial_factory(port=None, baudrate=baud, timeout=0.5)
    port.dtr = True
    port.rts = True
    port.port = device
    try:
        port.open()
        port.rts = False
        port.dtr = False
        if not disable_hangup_on_close(port):
            print("[serial-tail] Host does not support disabling hangup-on-close.", file=sys.stderr)
        return port
    except Exception:
        port.close()
        raise


def capture(port, seconds, outputs, clock=time.monotonic):
    """Read chunks instead of lines so a missing newline cannot extend the deadline."""
    deadline = clock() + seconds
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    while True:
        remaining = deadline - clock()
        if remaining <= 0:
            break
        port.timeout = min(0.5, remaining)
        data = port.read(4096)
        if data:
            text = decoder.decode(data)
            for output in outputs:
                output.write(text)
                output.flush()
    final = decoder.decode(b"", final=True)
    if final:
        for output in outputs:
            output.write(final)
            output.flush()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=None)
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", help="Also save captured serial output to this UTF-8 file.")
    args = parser.parse_args(argv)
    if not 0 < args.seconds < float("inf"):
        parser.error("--seconds must be a finite positive duration")
    if args.baud <= 0:
        parser.error("--baud must be positive")

    devices = sorted(glob.glob("/dev/cu.usbmodem*")) if args.port is None else [args.port]
    if len(devices) != 1:
        parser.error("Specify --port: no unique USB serial device was found.")
    try:
        import serial
    except ImportError:
        print("pyserial is required. Run with .tools/serial-env/bin/python or uv run --with pyserial.", file=sys.stderr)
        return 1

    try:
        with contextlib.ExitStack() as cleanup:
            outputs = [sys.stdout]
            if args.output:
                outputs.append(cleanup.enter_context(open(args.output, "w", encoding="utf-8")))
            port = open_monitor(devices[0], args.baud, serial.Serial)
            cleanup.callback(port.close)
            capture(port, args.seconds, outputs)
    except KeyboardInterrupt:
        return 130
    except OSError as error:
        print(f"[serial-tail] Capture stopped: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
