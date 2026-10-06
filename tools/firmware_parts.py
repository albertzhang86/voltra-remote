"""Validated binary list shared by packaging and developer upload."""
from pathlib import Path
import csv

def firmware_parts(build: Path):
    parts=[('0x0','VoltraKnob.ino.bootloader.bin'),('0x8000','VoltraKnob.ino.partitions.bin'),('0xe000','boot_app0.bin'),('0x10000','VoltraKnob.ino.bin')]
    for row in csv.reader((build/'partitions.csv').read_text().splitlines()):
        if not row or row[0].strip().startswith('#'): continue
        if row[0].strip()=='model':
            offset,size=int(row[3].strip(),0),int(row[4].strip(),0)
            model=build/'srmodels.bin'
            if not model.is_file() or not 0<model.stat().st_size<=size:
                raise ValueError('Missing or oversized speech model: rebuild before flashing/packaging')
            parts.append((hex(offset),'srmodels.bin'))
    for _,name in parts:
        if not (build/name).is_file(): raise FileNotFoundError(build/name)
    return parts
