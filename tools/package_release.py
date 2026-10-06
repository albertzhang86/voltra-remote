#!/usr/bin/env python3
"""Package the current build and portable installer; no device access."""
from pathlib import Path
import argparse, hashlib, json, shutil, zipfile
from firmware_parts import firmware_parts
ROOT=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--name',required=True,help='New release folder name; existing releases are never overwritten')
NAME=parser.parse_args().name
if not NAME.startswith('Voltra-Remote-') or Path(NAME).name!=NAME: raise ValueError('Invalid release name')
# Private replay audio must never enter a shareable source or firmware bundle.
if (ROOT/'firmware/VoltraKnob/src/voice/private_replay_fixture.h').exists():
    raise RuntimeError('Remove the private voice replay fixture and rebuild without VOLTRA_VOICE_REPLAY before packaging')
config=ROOT/'firmware/VoltraKnob/src/voice/replay_config.h'
if config.exists() and '#define VOLTRA_VOICE_REPLAY' in config.read_text():
    raise RuntimeError('Disable the local replay build before packaging')
app=ROOT/'build/VoltraKnob/VoltraKnob.ino.bin'
if app.exists() and b'[replay] trial=' in app.read_bytes():
    raise RuntimeError('Diagnostic voice replay binary cannot be packaged; rebuild normal firmware first')
DEST=ROOT/'releases'/NAME
if DEST.exists() or DEST.with_suffix(".zip").exists(): raise FileExistsError(DEST)
DEST.mkdir(parents=True)
for file in (ROOT/'distribution').iterdir():
    if file.is_file(): shutil.copy2(file,DEST/file.name)
parts=[]
for offset,name in firmware_parts(ROOT/'build/VoltraKnob'):
    target=DEST/'firmware'/name;target.parent.mkdir(exist_ok=True)
    shutil.copy2(ROOT/'build/VoltraKnob'/name,target)
    parts.append({'offset':offset,'file':'firmware/'+name})
manifest={'name':NAME,'board':'Guition JC3636K718 ESP32-S3 16MB','parts':parts,
          'sha256':{p['file']:hashlib.sha256((DEST/p['file']).read_bytes()).hexdigest() for p in parts}}
(DEST/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
source=DEST/'source';source.mkdir(exist_ok=True)
ignore=shutil.ignore_patterns('.git','.DS_Store','__pycache__','*.pyc','node_modules','icon-references')
for folder in ['firmware','assets','third_party','tools','test','distribution','.github','release-evidence']:
    shutil.copytree(ROOT/folder,source/folder,dirs_exist_ok=True,ignore=ignore)
for name in ['README.md','THIRD_PARTY.md','LICENSE','arduino-cli.yaml','.gitignore','BUILDING.md','TESTING.md','RELEASE_NOTES.md','GITHUB-RELEASE.md']:
    shutil.copy2(ROOT/name,source/name)
for name in ['RELEASE_NOTES.md','BUILDING.md','TESTING.md','GITHUB-RELEASE.md']:
    shutil.copy2(ROOT/name,DEST/name)
zip_path=DEST.parent/(NAME+'.zip')
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as out:
    for file in sorted(DEST.rglob('*')):
        if file.is_file():out.write(file,Path(NAME)/file.relative_to(DEST))
digest=hashlib.sha256(zip_path.read_bytes()).hexdigest()
zip_path.with_suffix('.zip.sha256').write_text(digest+'  '+zip_path.name+'\n')
print(zip_path)
print(f'{zip_path.stat().st_size/1024/1024:.1f} MB; SHA256 {digest}')

source_zip=DEST.parent/(NAME+'-source.zip')
with zipfile.ZipFile(source_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as out:
    for file in sorted(source.rglob('*')):
        if file.is_file():out.write(file,Path('Voltra-Remote')/file.relative_to(source))
source_zip.with_suffix('.zip.sha256').write_text(hashlib.sha256(source_zip.read_bytes()).hexdigest()+'  '+source_zip.name+'\n')
print(source_zip)
