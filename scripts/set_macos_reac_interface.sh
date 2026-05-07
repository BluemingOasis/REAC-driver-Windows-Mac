#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run with sudo: sudo $0 <interface>" >&2
  exit 1
fi

interface="${1:-}"
if [[ -z "$interface" ]]; then
  echo "Usage: sudo $0 <interface>" >&2
  echo "Example: sudo $0 en7" >&2
  exit 1
fi

defaults write /Library/Preferences/com.reac.decoder captureInterface -string "$interface"
chmod 644 /Library/Preferences/com.reac.decoder.plist
chown root:wheel /Library/Preferences/com.reac.decoder.plist

echo "Set system REAC captureInterface=$interface"
echo "Restart Core Audio or reselect REAC 40ch for the driver to pick it up."
