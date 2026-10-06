#!/usr/bin/env python3
"""Generate private on-device replay fixtures from local eight-second WAVs.
Remove the generated header and rebuild normally before packaging a release.
"""
import argparse
from pathlib import Path
import struct
import wave


def read_clip(path):
    with wave.open(str(path), 'rb') as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) != (1, 2, 16000, 128000):
            raise ValueError(f'{path}: expected eight seconds of mono 16-bit PCM at 16 kHz')
        samples = struct.unpack('<128000h', wav.readframes(wav.getnframes()))
    if max(abs(v) for v in samples) < 100:
        raise ValueError(f'{path}: speech clip is silent or too quiet')
    return samples


p = argparse.ArgumentParser(description=__doc__)
p.add_argument('wav', type=Path, help='Recorded release regression clip')
p.add_argument('--negative', type=Path, required=True, help='Second comparison clip (mixed commands for load/release tests)')
p.add_argument('--confusable', type=Path, required=True, help='Unrelated/confusable speech clip')
a = p.parse_args()
clips = [('replay_samples', read_clip(a.wav)), ('replay_negative', read_clip(a.negative)), ('replay_confusable', read_clip(a.confusable))]
path = Path(__file__).resolve().parent.parent / 'firmware/VoltraKnob/src/voice/private_replay_fixture.h'
with path.open('w') as output:
    output.write('// PRIVATE diagnostic audio. Generated; remove before packaging.\n#pragma once\n#include <cstdint>\n#include <cstddef>\n')
    for name, samples in clips:
        output.write(f'static const int16_t {name}[]={{\n')
        for i in range(0, len(samples), 32):
            output.write(','.join(map(str, samples[i:i + 32])) + ',\n')
        output.write('};\n')
    output.write('constexpr size_t replay_sample_count=sizeof(replay_samples)/sizeof(replay_samples[0]);\n')
print(path)
