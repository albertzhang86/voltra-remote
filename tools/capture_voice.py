#!/usr/bin/env python3
"""Retrieve one explicitly screen-triggered, eight-second local mic diagnostic.
No remote services; raw and AFE clips have separate start times, not stereo alignment.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import time
import wave
import serial


def decode_data(line):
    match = re.fullmatch(rb"\[audio-data\] ([RP]) (\d+) ([0-9a-f]+)\r?\n", line)
    if not match:
        return None
    channel, offset, encoded = match.groups()
    data = bytes.fromhex(encoded.decode())
    if len(data) != 256:
        raise ValueError("Unexpected audio block size")
    return channel.decode(), int(offset), data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=180)
    args = parser.parse_args()
    buffers = {'R': bytearray(), 'P': bytearray()}
    rate, samples, receiving = 16000, 128000, False
    deadline = time.monotonic() + args.seconds
    request_at = 0
    print('Waiting for the screen-triggered 8-second capture.', flush=True)
    with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=1) as port:
        port.write(b'VOICE_ARM_CAPTURE\n')
        while time.monotonic() < deadline:
            now = time.monotonic()
            if not receiving and now >= request_at:
                port.write(b'VOICE_DUMP\n')
                request_at = now + 3
            line = port.readline()
            if line.startswith(b'[audio-begin]'):
                if line.strip() != b'[audio-begin] rate=16000 samples=128000':
                    raise ValueError('Unexpected capture format')
                buffers = {'R': bytearray(), 'P': bytearray()}
                receiving = True
                print('Receiving captured audio over USB.', flush=True)
                continue
            if not receiving:
                continue
            block = decode_data(line)
            if block:
                channel, offset, data = block
                if offset * 2 != len(buffers[channel]):
                    raise ValueError('Missing or duplicated audio block; rerun to retrieve again')
                buffers[channel].extend(data)
            if line.strip() == b'[audio-end]':
                if any(len(data) != samples * 2 for data in buffers.values()):
                    raise ValueError('Incomplete audio capture')
                break
        else:
            raise TimeoutError('No complete capture received; rerun after tapping the mic test label')
    args.output.mkdir(parents=True, exist_ok=True)
    summary = {}
    for channel, name in [('R', 'microphone-raw'), ('P', 'recognizer-input')]:
        data = buffers[channel]
        path = args.output / (name + '.wav')
        with wave.open(str(path), 'wb') as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(rate)
            wav.writeframes(data)
        values = struct.unpack('<' + 'h' * samples, data)
        mean = sum(values) / samples
        summary[name] = {
            'samples': samples, 'seconds': samples / rate,
            'peak': max(abs(value) for value in values),
            'dc': mean,
            'ac_rms': (sum((value - mean) ** 2 for value in values) / samples) ** 0.5,
            'clipped_samples': sum(abs(value) >= 32767 for value in values),
        }
        print(path.resolve(), flush=True)
    (args.output / 'measurements.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
