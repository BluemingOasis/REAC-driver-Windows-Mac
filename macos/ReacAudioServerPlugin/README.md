# REAC Core Audio Driver Scaffold

The macOS end goal is a Core Audio `AudioServerPlugIn` that exposes:

- 40 input channels from the REAC packet stream
- 2 monitor output channels routed to a selected Core Audio output device
- 48 kHz sample rate

This folder is a scaffold and design note for the Mac-side driver. The first
thing to run on the Mac is the C++ probe target from the repo root:

```bash
cmake -S . -B build
cmake --build build --target reac_macos_probe
sudo ./build/reac_macos_probe --list-devices
sudo ./build/reac_macos_probe --device en0 --seconds 10
```

The probe must decode roughly 4000 packets per second before the Core Audio
plugin work is worth debugging.

## Settings

The SwiftUI settings app writes:

```text
suite: com.reac.decoder
captureInterface = en0
monitorOutputDeviceID = <CoreAudio AudioDeviceID>
```

The AudioServerPlugIn should read the same preferences.

## Plugin Work Remaining On Mac

1. Create an Xcode bundle target with bundle type `com.apple.audio-server-plugin`.
2. Implement the Core Audio plugin COM interface.
3. Feed the existing `reac::Decoder` and `ReacRingBuffer` from a `pcap` capture thread.
4. Expose stream/channel layout as 40 mono inputs and 2 outputs.
5. Install to one of:

```text
~/Library/Audio/Plug-Ins/HAL/ReacAudioServerPlugin.driver
/Library/Audio/Plug-Ins/HAL/ReacAudioServerPlugin.driver
```

6. Restart Core Audio:

```bash
sudo killall coreaudiod
```

This has to be compiled and debugged on macOS because the Core Audio HAL plugin
ABI and installation behavior are not testable from Windows.
