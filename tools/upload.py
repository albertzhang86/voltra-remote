#!/usr/bin/env python3
"""Upload a verified build, including any speech models. Use flash.sh for a backup."""
from pathlib import Path
import argparse,subprocess
from firmware_parts import firmware_parts
root=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True);args=p.parse_args()
build=root/'build/VoltraKnob'
parts=firmware_parts(build)
command=[str(root/'.arduino/data/packages/esp32/tools/esptool_py/5.3.1/esptool'),'--chip','esp32s3','--port',args.port,'--baud','921600','write-flash','--flash-mode','keep','--flash-freq','keep','--flash-size','keep']
for offset,name in parts: command.extend([offset,str(build/name)])
subprocess.run(command,check=True)
