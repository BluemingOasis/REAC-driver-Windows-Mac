#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run with sudo: sudo $0" >&2
  exit 1
fi

install_root="/Library/Application Support/REAC"
plist="/Library/LaunchDaemons/com.reac.decoder.bpf.plist"

mkdir -p "$install_root"
cp "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/enable_macos_bpf_for_coreaudio.sh" "$install_root/enable_macos_bpf_for_coreaudio.sh"
chmod 755 "$install_root/enable_macos_bpf_for_coreaudio.sh"
chown root:wheel "$install_root/enable_macos_bpf_for_coreaudio.sh"

cat > "$plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.reac.decoder.bpf</string>
  <key>ProgramArguments</key>
  <array>
    <string>$install_root/enable_macos_bpf_for_coreaudio.sh</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
  <key>StandardOutPath</key>
  <string>/var/log/reac-bpf.log</string>
  <key>StandardErrorPath</key>
  <string>/var/log/reac-bpf.log</string>
</dict>
</plist>
PLIST

chmod 644 "$plist"
chown root:wheel "$plist"

launchctl bootout system "$plist" >/dev/null 2>&1 || true
launchctl bootstrap system "$plist"
launchctl kickstart -k system/com.reac.decoder.bpf

echo "Installed and started $plist"
