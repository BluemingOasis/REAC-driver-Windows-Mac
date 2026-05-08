# macOS REAC Core Audio Setup

This repo includes a macOS Core Audio HAL driver that exposes the REAC stream as
`REAC 40ch`:

- 40 input channels from REAC packets
- 2 silent output channels so DAWs can open the device as input/output
- 48 kHz sample rate

The driver captures raw Ethernet frames with `pcap`/BPF, decodes them with the
shared REAC decoder, and feeds Core Audio from a background capture thread.

## 1. Build and Test Packet Capture

Install Xcode command line tools:

```bash
xcode-select --install
```

Build the probe:

```bash
./scripts/build_macos_probe.sh
```

List network devices:

```bash
sudo ./build/reac_macos_probe --list-devices
```

Run a 10-second REAC decode test, replacing `en7` with your REAC Ethernet port:

```bash
sudo ./build/reac_macos_probe --device en7 --seconds 10
```

Expected result is roughly 40000 decoded packets in 10 seconds.

## 2. Create a Local Signing Identity

Recent macOS versions reject unsigned or ad-hoc Core Audio HAL drivers. For local
development, create and trust a local code-signing certificate:

```bash
./scripts/create_macos_local_codesign_identity.sh
```

The script imports `REAC Local Root Code Signing` into your login keychain and
adds its certificate as a trusted code-signing root in the System keychain.

If you already have a suitable signing identity, skip this and build with:

```bash
REAC_CODESIGN_IDENTITY="Your Signing Identity" ./scripts/build_macos_audio_driver.sh
```

## 3. Build and Install the Driver

```bash
./scripts/build_macos_audio_driver.sh
sudo ./scripts/install_macos_audio_driver.sh --system
sudo killall coreaudiod
```

The installed driver path is:

```text
/Library/Audio/Plug-Ins/HAL/ReacAudioServerPlugin.driver
```

After restart, `REAC 40ch` should appear in Audio MIDI Setup and DAWs.

## 4. Configure the REAC Port

The HAL driver runs as `_coreaudiod`, so it reads the system preference:

```bash
sudo ./scripts/set_macos_reac_interface.sh en7
sudo killall coreaudiod
```

The current SwiftUI config app writes user preferences, which are useful for the
app but are not yet the source of truth for the HAL driver. Until the app gains a
privileged helper, use `set_macos_reac_interface.sh` for the driver.

## 5. Enable Capture After Reboot

macOS creates `/dev/bpf*` devices as `root:wheel` with mode `600`. The REAC HAL
driver runs as `_coreaudiod`, so it needs BPF access before it can capture
packets.

For one session:

```bash
sudo ./scripts/enable_macos_bpf_for_coreaudio.sh
```

To install a boot helper that applies this automatically:

```bash
sudo ./scripts/install_macos_bpf_launchdaemon.sh
```

This is a development-oriented setup. A production-quality driver should use a
privileged capture helper or system extension and feed the HAL driver through
shared memory or IPC.

## 6. Optional Desktop Launcher

Install a double-clickable launcher:

```bash
./scripts/install_macos_desktop_launcher.sh
```

This creates `~/Desktop/REAC Control.command`. It opens a simple macOS dialog
menu where you can:

- start REAC
- stop REAC capture access
- choose the network port
- view status

## 7. DAW Use

1. Run `REAC Control.command` and choose `Start REAC`, or run the setup commands
   above manually.
2. Open your DAW.
3. Select `REAC 40ch` as the audio device.
4. Set the sample rate to 48 kHz.
5. Arm or monitor input channels 1-40.

Capture starts when Core Audio starts IO for the device.

To watch the driver decode packets:

```bash
log stream --style compact --predicate 'eventMessage CONTAINS[c] "REAC capture"'
```
