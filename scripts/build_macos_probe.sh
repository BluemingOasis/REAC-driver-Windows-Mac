#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

mkdir -p "$repo_root/build"

clang++ \
  -std=c++17 \
  -Isrc \
  -Isrc/macos \
  src/macos/reac_macos_probe.cpp \
  src/macos/macos_pcap_capture.cpp \
  src/reac_decoder.cpp \
  -lpcap \
  -o build/reac_macos_probe

echo "Built build/reac_macos_probe"
