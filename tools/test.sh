#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC=firmware/VoltraKnob/src
CXX="clang++ -std=c++17 -Wall -Wextra -I$SRC -Itest"
$CXX test/test_protocol.cpp $SRC/protocol/voltra_protocol.cpp -o build/host/test_protocol
build/host/test_protocol
if [ -f test/test_app_logic.cpp ]; then
  $CXX test/test_app_logic.cpp $SRC/app/app_logic.cpp -o build/host/test_app_logic
  build/host/test_app_logic
fi

$CXX test/test_remote.cpp $SRC/protocol/voltra_protocol.cpp -o build/host/test_remote
build/host/test_remote

$CXX test/test_direction_filter.cpp -o build/host/test_direction_filter
build/host/test_direction_filter

$CXX test/test_voice.cpp -o build/host/test_voice
build/host/test_voice

$CXX test/test_voice_audio.cpp -o build/host/test_voice_audio
build/host/test_voice_audio

$CXX test/test_voice_capture.cpp -o build/host/test_voice_capture
build/host/test_voice_capture
