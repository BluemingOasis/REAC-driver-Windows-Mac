#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER_PATH="/Library/Audio/Plug-Ins/HAL/ReacAudioServerPlugin.driver"
BPF_PLIST="/Library/LaunchDaemons/com.reac.decoder.bpf.plist"

cd "$REPO_ROOT"

dialog() {
  osascript -e "display dialog \"$1\" buttons {\"OK\"} default button \"OK\" with title \"REAC Control\""
}

choose_action() {
  osascript <<'APPLESCRIPT'
set actionList to {"Start REAC", "Stop REAC", "Choose Port Only", "Status", "Quit"}
set picked to choose from list actionList with title "REAC Control" with prompt "What do you want to do?" default items {"Start REAC"} OK button name "Continue" cancel button name "Quit"
if picked is false then
  return "Quit"
end if
return item 1 of picked
APPLESCRIPT
}

choose_port() {
  local interfaces
  interfaces="$(ifconfig -l | tr ' ' '\n' | awk '/^en[0-9]+$/ { print }' | sort -V | paste -sd, -)"
  if [[ -z "$interfaces" ]]; then
    interfaces="en7,en0"
  fi

  osascript <<APPLESCRIPT
set AppleScript's text item delimiters to ","
set portList to text items of "$interfaces"
set picked to choose from list portList with title "REAC Control" with prompt "Select the Ethernet port connected to REAC:" default items {"$(current_port)"} OK button name "Use Port" cancel button name "Cancel"
if picked is false then
  return ""
end if
return item 1 of picked
APPLESCRIPT
}

current_port() {
  defaults read /Library/Preferences/com.reac.decoder captureInterface 2>/dev/null || echo "en7"
}

require_port() {
  local port
  port="$(choose_port)"
  if [[ -z "$port" ]]; then
    dialog "Cancelled."
    exit 0
  fi
  printf '%s\n' "$port"
}

start_reac() {
  local port="$1"
  echo "Starting REAC on $port..."
  sudo "$REPO_ROOT/scripts/set_macos_reac_interface.sh" "$port"
  sudo "$REPO_ROOT/scripts/install_macos_bpf_launchdaemon.sh"
  sudo "$REPO_ROOT/scripts/enable_macos_bpf_for_coreaudio.sh"

  if [[ -d "$REPO_ROOT/build/ReacAudioServerPlugin.driver" ]]; then
    sudo "$REPO_ROOT/scripts/install_macos_audio_driver.sh" --system
    sudo xattr -dr com.apple.quarantine "$DRIVER_PATH" 2>/dev/null || true
  fi

  sudo killall coreaudiod 2>/dev/null || true
  dialog "REAC is ready on $port. In your DAW, select the audio device named REAC 40ch."
}

stop_reac() {
  echo "Stopping REAC capture access..."
  sudo launchctl bootout system "$BPF_PLIST" >/dev/null 2>&1 || true
  sudo chgrp wheel /dev/bpf* 2>/dev/null || true
  sudo chmod 600 /dev/bpf* 2>/dev/null || true
  sudo killall coreaudiod 2>/dev/null || true
  dialog "REAC capture access is stopped. The audio device bundle remains installed for the next start."
}

set_port_only() {
  local port="$1"
  sudo "$REPO_ROOT/scripts/set_macos_reac_interface.sh" "$port"
  sudo killall coreaudiod 2>/dev/null || true
  dialog "REAC port set to $port."
}

show_status() {
  local port bpf_state driver_state daemon_state
  port="$(current_port)"
  if [[ -d "$DRIVER_PATH" ]]; then
    driver_state="installed"
  else
    driver_state="not installed"
  fi

  if ls -l /dev/bpf* 2>/dev/null | grep -q "_coreaudiod"; then
    bpf_state="enabled for Core Audio"
  else
    bpf_state="not enabled for Core Audio"
  fi

  if launchctl print system/com.reac.decoder.bpf >/dev/null 2>&1; then
    daemon_state="installed and loaded"
  else
    daemon_state="not loaded"
  fi

  osascript <<APPLESCRIPT
display dialog "Port: $port
Driver: $driver_state
BPF access: $bpf_state
Boot helper: $daemon_state

Open your DAW and select REAC 40ch to start audio capture." buttons {"OK"} default button "OK" with title "REAC Status"
APPLESCRIPT
}

while true; do
  action="$(choose_action)"
  case "$action" in
    "Start REAC")
      start_reac "$(require_port)"
      ;;
    "Stop REAC")
      stop_reac
      ;;
    "Choose Port Only")
      set_port_only "$(require_port)"
      ;;
    "Status")
      show_status
      ;;
    "Quit")
      exit 0
      ;;
  esac
done
