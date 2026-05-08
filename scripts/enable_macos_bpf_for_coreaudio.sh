#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Run with sudo: sudo $0" >&2
  exit 1
fi

shopt -s nullglob
bpf_devices=(/dev/bpf*)
if [[ "${#bpf_devices[@]}" -eq 0 ]]; then
  echo "No /dev/bpf* devices found." >&2
  exit 1
fi

chgrp _coreaudiod "${bpf_devices[@]}"
chmod 660 "${bpf_devices[@]}"

echo "Granted _coreaudiod read/write access to ${#bpf_devices[@]} BPF device(s)."
echo "This is a development-only setting and may reset after reboot."
