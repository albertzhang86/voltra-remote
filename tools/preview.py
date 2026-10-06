#!/usr/bin/env python3
"""Render the actual LVGL firmware UI natively; no BLE or device writes."""
from pathlib import Path
import subprocess,concurrent.futures
root=Path(__file__).resolve().parent.parent
src=root/'firmware/libraries/lvgl/src'
out=root/'build/preview-objects';out.mkdir(parents=True,exist_ok=True)
includes=['-I'+str(root/'firmware/libraries/lvgl'),'-I'+str(root/'firmware/libraries'),'-DLV_CONF_INCLUDE_SIMPLE']
def compile(path):
 obj=out/(str(path.relative_to(src)).replace('/','_')+'.o')
 if not obj.exists() or obj.stat().st_mtime<path.stat().st_mtime:
  subprocess.run(['clang','-O1','-w',*includes,'-c',str(path),'-o',str(obj)],check=True)
 return str(obj)
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:objs=list(pool.map(compile,src.rglob('*.c')))
for font in (root/'firmware/VoltraKnob/src/ui').glob('*.c'):
 obj=out/(font.stem+'.o')
 subprocess.run(['clang','-O1',*includes,'-c',str(font),'-o',str(obj)],check=True)
 objs.append(str(obj))
subprocess.run(['clang++','-std=c++17','-O1',*includes,'-I'+str(root/'test/host'),'-I'+str(root/'firmware/VoltraKnob/src'),str(root/'test/host/ui_preview.cpp'),str(root/'firmware/VoltraKnob/src/ui/ui.cpp'),*objs,'-o',str(root/'build/ui-preview')],check=True)
subprocess.run([str(root/'build/ui-preview')],cwd=root,check=True)
print('Rendered actual firmware screens in build/preview-*.ppm')
