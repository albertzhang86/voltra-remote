#!/usr/bin/env python3
"""Interactive installer for the 16 MB Guition JC3636K718 ESP32-S3 knob."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys, time, venv
ROOT = Path(__file__).resolve().parent

def verify():
    manifest = json.loads((ROOT / 'manifest.json').read_text())
    for name, expected in manifest['sha256'].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError('Checksum mismatch: ' + name + '. Extract a fresh copy of the ZIP.')
    print('All packaged firmware checksums verified.')
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only', action='store_true', help='Check package files without connecting to hardware')
    args = parser.parse_args()
    manifest = verify()
    if args.verify_only: return
    if sys.version_info < (3, 10): raise RuntimeError('Install Python 3.10 or newer from python.org first.')
    env = ROOT / '.installer-env'
    python = env / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
    if Path(sys.prefix).resolve() != env.resolve():
        if not python.exists(): venv.EnvBuilder(with_pip=True).create(env)
        check = subprocess.run([str(python), '-c', 'import esptool, serial'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if check.returncode:
            subprocess.run([str(python), '-m', 'pip', 'install', 'esptool>=5,<6'], check=True)
        subprocess.run([str(python), str(Path(__file__).resolve())], check=True)
        return
    from serial.tools import list_ports
    ports = sorted(list(list_ports.comports()), key=lambda p: p.device)
    if not ports: raise RuntimeError('No serial ports found. Use a USB data cable and disable factory HID/Udisk; enter download mode if needed.')
    print('\nFor Guition JC3636K718 / JC3636K718C, ESP32-S3, 360x360, 16 MB flash only.')
    for i, port in enumerate(ports, 1): print(f'{i}. {port.device} — {port.description}')
    selection = int(input('Choose the knob port number: ')) - 1
    if selection < 0 or selection >= len(ports): raise RuntimeError('Invalid port number.')
    port = ports[selection].device
    print('\nThis backs up your knob, replaces its firmware, and clears saved settings/pairings.')
    if input('Type INSTALL to proceed: ').strip() != 'INSTALL':
        print('Cancelled. No device changes made.'); return
    base = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', port, '--baud', '115200']
    def run(*parts): subprocess.run(base + list(parts), check=True, cwd=ROOT)
    folder = ROOT / 'backups'; folder.mkdir(exist_ok=True)
    backup = folder / ('original-' + time.strftime('%Y%m%d-%H%M%S') + '.bin')
    print('Reading the complete original flash. This may take several minutes.')
    run('read-flash', '0', 'ALL', str(backup))
    if backup.stat().st_size != 16 * 1024 * 1024:
        raise RuntimeError('This is not a 16 MB flash device. Backup saved; installation stopped before any writes.')
    digest = hashlib.sha256(backup.read_bytes()).hexdigest()
    backup.with_suffix('.bin.sha256').write_text(digest + '  ' + backup.name + '\n')
    # Clear NVS only after a successful, complete backup. Firmware contains no trainer identities.
    run('erase-region', '0x9000', '0x5000')
    parts = ['write-flash', '--flash-mode', 'dio', '--flash-freq', '80m', '--flash-size', '16MB']
    for item in manifest['parts']: parts.extend([item['offset'], str(ROOT / item['file'])])
    run(*parts)
    print('\nInstallation finished. Keep your backup:', backup)
    print('Restart the knob, close Beyond+, power on your trainers, and select one on the knob.')

if __name__ == '__main__':
    try: main()
    except (Exception, KeyboardInterrupt) as error:
        print('\nInstallation stopped:', error, file=sys.stderr)
        sys.exit(1)
