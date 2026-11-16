# Vita A2DP SBC Receiver v0.3 — experimental

A first standalone attempt to decode SBC Bluetooth music sent by an iPhone
to a jailbroken PS Vita 1000/2000 (firmware 3.65).

The actual hardware log identified the A2DP media RTP packets as:
- RTP header starts `80 60`
- Payload header `08`: eight SBC frames
- SBC sync `9c`, frame header `bd 33`
- 48 kHz, joint stereo, 16 blocks, eight subbands, bitpool 51
- 115 bytes/frame × 8 + 13 bytes RTP/A2DP framing = 933 bytes/packet

This build performs:
SceBt incoming L2CAP hook -> bounded RTP ring -> userspace syscall ->
Google libsbc decoder -> sceAudioOut 48kHz stereo.
The existing VitaPlay CarPlay connection is NOT used.

## Limitations

**Built and statically packaged, but not hardware verified for audio.**
The kernel component is reverse-engineered, unsafe if it crashes. Do not
consider this a finished Bluetooth audio sink. The playback app supports
only the known iPhone SBC format (8 frames/packet, RTP PT 96,
no CSRC or extension). Other phones may differ.
The receiver does not enable new Bluetooth profiles and does not send
AVDTP commands or implement microphone/HFP.
If iPhone doesn't pair with Vita, that is not fixed by this app.

## Installation

1. Back up your active taiHEN config and make sure you can recover from
   kernel boot problems. Many Ensō setups use holding L while booting to
   skip plugins; check your console's setup.
2. Copy `a2dp_probe.skprx` to `ur0:tai/a2dp_probe.skprx`.
3. Under `*KERNEL` in the **active** `tai/config.txt`, enable
   `ur0:tai/a2dp_probe.skprx`. Disable the previous version of the probe,
   and temporarily disable `vitaplaybt.skprx` so both do not run.
4. Reboot the Vita. Install `VitaA2DP.vpk` via VitaShell.
5. Open **Vita A2DP SBC**. Keep VitaPlay closed.
6. On iPhone, connect/select **PlayStation Vita** as Bluetooth audio
   output. Play a short music sample.
7. Look for Incoming RTP packets and Decoded PCM samples increasing.
8. Press START to quit. If still silent, send both:
   `ux0:data/VitaPlay/sbc_playback.log` and
   `ux0:data/VitaPlay/a2dp_probe_v02.log`.

## Rollback

Remove the probe entry from `*KERNEL`, re-enable your original
`vitaplaybt.skprx`, and reboot. Nothing in the CarPlay application or
MFi identity files is modified by this build.

## Third-party code

This package links **Google libsbc**, Apache License 2.0,
revision `6e505650145c9973d08a0bdd5e5f5e1914305e40`.
See included `Google-libsbc-LICENSE` in ZIP distribution.
Source: https://github.com/google/libsbc

The Vita A2DP probe and SBC receiver source is available in the
`experiment/vita-a2dp-sbc-playback-20261117` branch.
