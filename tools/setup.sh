#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .tools
if [[ ! -x .tools/arduino-cli ]]; then
  archive=$(mktemp -t voltra-arduino)
  curl -fLsS https://downloads.arduino.cc/arduino-cli/arduino-cli_1.5.1_macOS_ARM64.tar.gz -o "$archive"
  tar -xzf "$archive" -C .tools arduino-cli
  rm "$archive"
fi
.tools/arduino-cli --config-file arduino-cli.yaml core update-index
.tools/arduino-cli --config-file arduino-cli.yaml core install esp32:esp32@3.3.11
.tools/arduino-cli --config-file arduino-cli.yaml lib install 'NimBLE-Arduino@2.5.1'
ctags_path=.arduino/data/packages/builtin/tools/ctags/5.8-arduino11/ctags
if ! "$ctags_path" --version >/dev/null 2>&1; then
  source_dir=$(mktemp -d -t voltra-ctags)
  git clone https://github.com/arduino/ctags.git "$source_dir"
  git -C "$source_dir" checkout abc8fca7499f44c725122881cd380a88c37abe0e
  python3 - "$source_dir" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1])
for p in root.iterdir():
    if p.suffix in ('.c','.h'):
        p.write_bytes(p.read_bytes().replace(b'__unused__',b'CTAGS_UNUSED'))
PY
  (cd "$source_dir" && ./configure && make -j8)
  cp "$source_dir/ctags" "$ctags_path"
fi
