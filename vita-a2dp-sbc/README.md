# Vita A2DP SBC Receiver v0.7 — fixed-rate playback test

Experimental standalone Bluetooth stereo receiver for the PS Vita running
firmware 3.65, based on the confirmed iPhone-to-Vita SBC stream.

The v0.5 hardware test produced audible but choppy audio. The v0.6 test
was significantly worse: its captured log had 1,903 underruns across
4,481 output blocks, 862 missing RTP sequence numbers and 29 PCM overflows.
The reported 3,563 kernel drops were already present at the first status
checkpoint and never increased during that run. They may have accumulated
before launch, so they should not be mistaken for fresh drops.

## v0.7 changes

- **No sample-by-sample float resampling.** Decoded 44.1-kHz PCM is copied
  directly into the 44.1-kHz BGM audio port.
- Uses a producer decoder thread and hardware-paced playback thread.
- Drops stale queued kernel RTP packets when starting, to avoid replaying
  audio from an earlier app instance. The kernel plugin is unchanged.
- Uses a short 186-ms prebuffer before starting, and re-buffers when
  incoming music pauses rather than constantly draining an empty buffer.
- Bounded 325-ms PCM queue to avoid multiple seconds of old playback.
- Minimal integer fades/concealment around a true dropout.
- Five-fps UI to reserve CPU for audio work.
- The log separates **new kernel drops since app launch** from the
  lifetime total and records incoming packets per ~3 seconds, RTP gaps,
  buffer occupancy, and rebuffer counts.

Codec is SBC, 44.1 kHz, joint stereo, bitpool 51. The known iPhone
stream uses 12-byte RTP header + 1-byte A2DP SBC header +
8 SBC frames of 115 bytes each (933 bytes per packet).

## Installation: v0.5 or v0.6 already installed

Install `VitaA2DP.vpk` over the existing application. The matching
kernel plugin `a2dp_probe.skprx` is **unchanged**, so do not replace
it or reboot the Vita. Leave VitaPlay itself closed during the test.

The VPK starts the standalone receiver; on iPhone, select
**PlayStation Vita** as audio output and play music. The log is:

`ux0:data/VitaPlay/sbc_playback.log`

For the best comparable test, open the receiver **before** starting music,
then let the music play for about 60 seconds. The app reports
**NEW kernel drops**, **RTP missing**, **Rebuffer**, and **PCM buffer ms**.
Press START to exit and upload the log. Music can still be choppy if
the iPhone sends RTP discontinuously, and v0.7 is not hardware-verified yet.

## New installation

**Experimental kernel code can crash the console.** Back up the active
taiHEN config and verify your recovery path first. The user previously
tested the companion plugin on a 3.65 Vita.

1. Copy `a2dp_probe.skprx` to `ur0:tai/a2dp_probe.skprx`.
2. In the active `tai/config.txt`, under `*KERNEL`, enable
   `ur0:tai/a2dp_probe.skprx`. Disable conflicting `vitaplaybt.skprx`
   for this standalone test.
3. Reboot and install `VitaA2DP.vpk`.
4. Launch the receiver; connect/select PlayStation Vita on your iPhone.
5. Collect `ux0:data/VitaPlay/sbc_playback.log` to diagnose playback.

## Reverting

Remove the A2DP probe from `*KERNEL`, restore your old VitaPlay
Bluetooth plugin line, and reboot. The standalone app has a separate
VPK and does not overwrite the CarPlay receiver or MFi identity.

## Third-party component

Google libsbc, Apache-2.0, pinned at revision
`6e505650145c9973d08a0bdd5e5f5e1914305e40`.
The archive contains the project's license.
Source: https://github.com/google/libsbc
