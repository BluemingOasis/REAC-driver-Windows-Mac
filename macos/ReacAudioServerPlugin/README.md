# REAC Core Audio HAL Driver

`ReacAudioServerPlugin.driver` is a Core Audio `AudioServerPlugIn` bundle for
macOS. It publishes a virtual device named `REAC 40ch`.

Current behavior:

- 40-channel interleaved input stream
- 2-channel output stream for DAW compatibility
- 48 kHz fixed sample rate
- live `pcap` capture on the configured REAC Ethernet interface
- REAC packet decode through the shared `reac::Decoder`
- zero-filled input on underrun or when capture is unavailable

The driver reads the capture interface from:

1. system preference `/Library/Preferences/com.reac.decoder.plist`
2. user/default app preference `com.reac.decoder`
3. environment variable `REAC_CAPTURE_INTERFACE`
4. fallback `en7`

Set the system preference with:

```bash
sudo ./scripts/set_macos_reac_interface.sh en7
```

Build and install from the repo root:

```bash
./scripts/build_macos_audio_driver.sh
sudo ./scripts/install_macos_audio_driver.sh --system
sudo killall coreaudiod
```

Watch runtime logs:

```bash
log stream --style compact --predicate 'eventMessage CONTAINS[c] "REAC capture" OR eventMessage CONTAINS[c] "ReacAudioServerPlugin"'
```

Known development caveat: packet capture from inside the HAL driver requires BPF
permissions for `_coreaudiod`. Use:

```bash
sudo ./scripts/install_macos_bpf_launchdaemon.sh
```

For a production-grade distribution, replace the BPF permission workaround with
a privileged capture helper or a system extension that streams decoded audio to
the HAL driver through shared memory or IPC.
