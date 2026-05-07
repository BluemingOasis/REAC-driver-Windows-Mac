#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bundle="$repo_root/build/ReacAudioServerPlugin.driver"
contents="$bundle/Contents"
macos="$contents/MacOS"

rm -rf "$bundle"
mkdir -p "$macos"
cp "$repo_root/macos/ReacAudioServerPlugin/Info.plist" "$contents/Info.plist"

clang++ \
  -std=c++17 \
  -I"$repo_root/src" \
  -I"$repo_root/src/macos" \
  -fvisibility=hidden \
  -bundle \
  -framework CoreAudio \
  -framework CoreFoundation \
  "$repo_root/macos/ReacAudioServerPlugin/ReacAudioServerPlugin.cpp" \
  "$repo_root/src/macos/macos_pcap_capture.cpp" \
  "$repo_root/src/reac_decoder.cpp" \
  "$repo_root/src/reac_ring_buffer.cpp" \
  -lpcap \
  -o "$macos/ReacAudioServerPlugin"

identity="${REAC_CODESIGN_IDENTITY:-}"
if [[ -z "$identity" ]] && security find-identity -v -p codesigning 2>/dev/null | grep -q "REAC Local Root Code Signing"; then
  identity="REAC Local Root Code Signing"
fi

if [[ -n "$identity" ]]; then
  codesign --force --sign "$identity" --timestamp=none "$bundle"
  echo "Signed $bundle with $identity"
else
  echo "Built unsigned bundle. Set REAC_CODESIGN_IDENTITY or run scripts/create_macos_local_codesign_identity.sh before installing."
fi

echo "Built $bundle"
