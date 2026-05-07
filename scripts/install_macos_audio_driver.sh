#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_bundle="$repo_root/build/ReacAudioServerPlugin.driver"

if [[ "${1:-}" == "--system" ]]; then
  install_root="/Library/Audio/Plug-Ins/HAL"
else
  install_root="$HOME/Library/Audio/Plug-Ins/HAL"
fi

target_bundle="$install_root/ReacAudioServerPlugin.driver"

if [[ ! -d "$source_bundle" ]]; then
  "$repo_root/scripts/build_macos_audio_driver.sh"
fi

mkdir -p "$install_root"
rm -rf "$target_bundle"
cp -R "$source_bundle" "$target_bundle"

echo "Installed $target_bundle"
echo "Restart Core Audio with: sudo killall coreaudiod"
