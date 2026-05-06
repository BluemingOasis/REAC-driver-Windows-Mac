# REAC Decoder Example

Small Windows-first REAC audio proof of concept.

This example expects raw Ethernet frames whose EtherType is `0x8819`. It decodes
Roland REAC 48 kHz, 40-channel, 24-bit packets into stereo float audio and plays
the chosen channel pair with the Windows `waveOut` API.

`reac_play` can listen directly through Npcap, or read packets from stdin for
debugging/replay.

## Prerequisite

Install Npcap on Windows. If you do not want to run the tool as administrator,
install Npcap with `Restrict Npcap driver's access to Administrators only`
unchecked.

Use a dedicated wired Ethernet adapter for the REAC network.

If `.\reac_play.exe --list-devices` says `Could not load wpcap.dll`, Npcap is
not installed or is not visible to normal desktop applications.

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Or with the MSYS2 compiler that is available on this machine:

```powershell
g++ -std=c++17 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -Isrc src\main.cpp src\pcap_capture.cpp src\reac_decoder.cpp src\waveout_player.cpp -lwinmm -o reac_play.exe
```

Build the settings GUI:

```powershell
g++ -std=c++17 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -mwindows -Isrc src\config_app.cpp src\pcap_capture.cpp src\reac_settings.cpp src\waveout_player.cpp -ladvapi32 -lwinmm -o reac_config.exe
```

## Listen Live

First list capture devices:

```powershell
.\reac_play.exe --list-devices
```

List Windows audio outputs:

```powershell
.\reac_play.exe --list-audio-devices
```

Then listen on the REAC Ethernet adapter. You can pass the device index from
the list, the full Npcap device name, or a unique piece of the description:

```powershell
.\reac_play.exe --device 7 --output "Speakers" --left 1 --right 2
```

On this machine, the REAC adapter is `Realtek PCIe GbE Family Controller` and
the laptop speakers are `Speakers (Realtek(R) Audio)`, so this is usually safer
than relying on adapter indices:

```powershell
.\reac_play.exe --device Realtek --output Speakers --left 1 --right 2
```

Channels are 1-based and must be in the range `1..40`.

For a short test run:

```powershell
.\reac_play.exe --device 7 --output "Speakers" --left 1 --right 2 --seconds 10
```

## Stdin Mode

Without `--device`, `reac_play` reads packets from stdin as:

```text
uint16 little-endian packet_length
packet_length bytes of one complete Ethernet frame
```

That makes it easy to connect to your existing raw packet receiver without
coupling the decoder to Npcap yet.

```powershell
your-packet-receiver.exe | .\reac_play.exe --left 1 --right 2
```

## Reaper ASIO Prototype

This repo also builds a prototype 40-input ASIO driver DLL for Reaper:

```powershell
g++ -std=c++17 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -Isrc -shared src\reac_asio_driver.cpp src\pcap_capture.cpp src\reac_decoder.cpp src\reac_ring_buffer.cpp -lole32 -ladvapi32 -luuid -o reac_asio.dll
```

Register it:

```powershell
.\scripts\register_reac_asio.ps1 -Device Realtek -Output Speakers
```

If Reaper does not show the driver, run machine-wide registration from an
elevated PowerShell:

```powershell
.\scripts\register_reac_asio.ps1 -Device Realtek -Output Speakers -Machine
```

The script also copies the needed MinGW runtime DLLs next to `reac_asio.dll`
when they are available locally, because Reaper may not inherit your shell PATH.

Then restart Reaper and choose:

```text
Options > Preferences > Audio > Device
Audio system: ASIO
ASIO Driver: REAC 40ch ASIO
```

The driver exposes 40 mono input channels named `REAC 01` through `REAC 40`.
Set track inputs in Reaper to the channels you want to record.

The capture adapter is selected by the user environment variable
`REAC_ASIO_DEVICE`; the register script sets it for you. Use a unique adapter
description fragment such as `Realtek`.

The ASIO driver also exposes two output channels and routes them to Windows
`waveOut` so you can monitor Reaper through the laptop speakers. The output
device is selected by `REAC_ASIO_OUTPUT`, usually `Speakers`. In Reaper, set the
master hardware output to `Silent Out 1 / Silent Out 2`; the name is plain, but
the driver sends those channels to the selected Windows speaker device.

Run the settings GUI to change the capture adapter or Reaper monitor output:

```powershell
.\reac_config.exe
```

You can also open it from Reaper using the ASIO driver control panel button.
After saving, restart Reaper or reselect the ASIO driver.

Unregister:

```powershell
.\scripts\unregister_reac_asio.ps1
```

This is a first ASIO prototype, not yet a production driver. The console player
is still the quickest way to verify packet decode and channel mapping.

## Protocol Notes

This follows the packet layout used by
`norihiro/obs-h8819-source`:

- Ethernet EtherType: `0x8819`
- Sampling rate: 48 kHz
- Channels: 40
- Samples per packet: 12
- Sample format: signed 24-bit PCM, converted to float `[-1.0, 1.0)`
- REAC payload starts at byte offset 54 of the Ethernet frame
- Valid packets end with bytes `C2 EA`

The upstream OBS plugin is GPL-3.0. This example is intentionally small and
keeps the decoder logic clear while we iterate.
