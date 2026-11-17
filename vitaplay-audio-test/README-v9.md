# VitaPlay v9 Hybrid — Bluetooth SBC music + Wi-Fi CarPlay video

**Goal:** keep the working CarPlay video over Wi-Fi and play iPhone music via
Bluetooth SBC directly within VitaPlay. **This is not a Wi-Fi audio fix.**

## What changed from v8

v8 disconnected Bluetooth on the iPhone's `disableBluetooth` command.
Your test established that the iPhone then had no working audio output on
the Vita. In v9 that forced disconnect is **removed** (v7 source baseline).

Instead, a **single updated `vitaplaybt.skprx` plugin** handles BOTH:
- Existing SceBt L2CAP/RFCOMM/iAP2 startup, pairing, MFi, and handoff-to-Wi-Fi
- A bounded Bluetooth A2DP SBC RTP packet queue (same SceBt receive hook)

VitaPlay includes Google libsbc as a native usermode codec, 44.1-kHz BGM
speaker output, 279ms prebuffer, 139ms rebuffering, no expensive resampler,
and packet-loss diagnostics. No separate standalone A2DP app or overlapping
kernel hooks are used. CarPlay video and input code remain unchanged.

The iPhone previously sent SBC frames (RTP payload 96, 8 SBC frames per
933-byte RTP packet, 44.1kHz stereo) to the Vita. This source specifically
decodes that observed format; it does not implement A2DP sink negotiation
or guarantee that iOS will deliver the packets when CarPlay is active.

## Installation — Vita 3.65 experimental kernel plugin

**Back up the working v7 VPK, original `vitaplaybt.skprx`, and active taiHEN
config. The merged kernel hook is hardware-unverified and could crash the Vita.**

1. Extract the ZIP. Copy the new `vitaplaybt.skprx` to the exact existing
   `*KERNEL` plugin path in the active `tai/config.txt`, usually
   `ur0:tai/vitaplaybt.skprx`. Save original first.
2. Disable the standalone `a2dp_probe.skprx` plugin and any older duplicate.
   Use exactly **one** VitaPlay Bluetooth plugin.
3. Reboot the Vita (kernel plugin must be loaded).
4. Install `VitaPlay-v9-BluetoothAudio-WiFiVideo.vpk` over the existing
   app without deleting its Wi-Fi/pairing/identity files.
5. Open VitaPlay and connect your iPhone to CarPlay. **Leave Bluetooth on**.
6. Start music, open iOS audio-output selector and **select PlayStation Vita**
   if needed. Do not manually disconnect BT. Test for a minute.
7. Send both:
   - `ux0:data/VitaPlay/log.txt`
   - `ux0:data/VitaPlay/l2capprobe.log`

## What logs prove

- `v9 hybrid mode: retaining Bluetooth`: the in-app hybrid receiver started
- `v9 hybrid SBC RTP cid=...`: the kernel is receiving BT music while Wi-Fi
  CarPlay is running (regardless of whether audio decoder starts)
- `v9 HYBRID PLAYBACK START`: decoded PCM arrived and the Vita BGM
  speaker port opened
- `v9 BT SOUND`: packet count, 44.1kHz buffer fill, RTP sequence gaps,
  kernel drops and underruns

**If video works but there's no SBC RTP counter, the remaining blocker is
A2DP routing/negotiation, not the speaker or decoder.** If RTP counts rise
but no playback appears, inspect `SBC format rejected` or port error.

## Limitations

- One known SBC 44.1kHz joint-stereo RTP layout only; other codecs require
  negotiated codec parsing.
- The Bluetooth route and CarPlay route may be controlled independently
  by iOS. Keeping the link does not guarantee iOS will transmit music.
- Apple normally expects `disableBluetooth` during Wi-Fi CarPlay. This
  hybrid experiment deliberately overrides that instruction to test your
  observed coexistence, and may not work with every iOS release.
- Siri, call audio, microphone input and native Wi-Fi CarPlay audio remain
  separate unfinished features.

## Rollback

Restore your backed-up `vitaplaybt.skprx`, reboot, and reinstall VitaPlay
v7. Do not delete MFi/identity or pairing data. Have a confirmed
kernel-plugin recovery procedure before installing v9.

## Sources

- VitaPlay upstream: https://github.com/ironmanx75/vitaplay-release
- SBC decoder: Google libsbc (Apache-2.0), pinned commit
  `6e505650145c9973d08a0bdd5e5f5e1914305e40`
  https://github.com/google/libsbc
