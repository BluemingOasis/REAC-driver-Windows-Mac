# macOS Plan

The shared REAC decoder is portable and already used by the Windows player and
ASIO driver. The macOS path uses the same decoder with different platform
plumbing:

- packet capture: `pcap` / BPF
- app settings: SwiftUI + `UserDefaults(suiteName: "com.reac.decoder")`
- multichannel audio device: Core Audio `AudioServerPlugIn`

## First Mac Test

Install Xcode command line tools:

```bash
xcode-select --install
```

Build the probe:

```bash
cmake -S . -B build
cmake --build build --target reac_macos_probe
```

List devices:

```bash
sudo ./build/reac_macos_probe --list-devices
```

Run a 10-second REAC packet decode test:

```bash
sudo ./build/reac_macos_probe --device en0 --seconds 10
```

Expected result is about 40000 packets in 10 seconds.

## Settings GUI

From `macos/ReacConfigApp`:

```bash
swift run ReacConfigApp
```

The app lets you choose the Ethernet interface and monitor output. It saves to:

```text
com.reac.decoder
captureInterface
monitorOutputDeviceID
```

## Core Audio Driver

See `macos/ReacAudioServerPlugin/README.md`. The driver scaffold/design is in
place, but the actual HAL plugin must be compiled and debugged on a Mac.
