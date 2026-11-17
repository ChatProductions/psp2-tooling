# VitaPlay v11 — v7 video + latest v10 SBC plugin, manually enabled

The question this test answers is **whether the newer Bluetooth kernel
plugin can coexist with VitaPlay's formerly working v7 CarPlay video**.

**Simply swapping the v10 kernel plugin into unchanged v7 DOES NOT add
sound**: v7 has no SBC packet-reader/decode calls and v10 plugin capture
defaults OFF. v11 keeps v7's original AirPlay video implementation and
uses a compatible usermode SBC reader, but gates the audio until **L+R**
is pressed.

v9 received and decoded SBC but did not initialize CarPlay video.
v10 waited for video before enabling audio, yet CarPlay video still did
not initialize, so kernel plugin interference or a broader iOS route/
network state remains possible. This build does NOT claim a working fix.

## Contents

- `VitaPlay-v11-V7-Video-Manual-BTAudio.vpk`: v7 video/AirPlay
  implementation with matching Bluetooth SBC decode/audio code
- `vitaplaybt.skprx`: the v10 Bluetooth RX kernel plugin, compiled
  with the same import/export library version as this VPK
- `README.md`: this installation and A/B test guide
- `Google-libsbc-LICENSE`: Apache-2.0 SBC license

## Safe test steps

1. **Back up** your known-working v7 VPK, active `tai/config.txt`,
   and original `vitaplaybt.skprx`. This plugin modifies a kernel
   Bluetooth receive hook, so know how to bypass/recover bad kernel plugins.
2. Copy the new `vitaplaybt.skprx` to your active `*KERNEL` VitaPlay
   plugin path (commonly `ur0:tai/vitaplaybt.skprx`). Do not enable any
   second SceBt/A2DP hook alongside it.
3. Reboot the Vita to load the updated plugin, then install the v11 VPK
   over the current VitaPlay without deleting identity/MFi data.
4. **Leave music stopped and iPhone audio output selected on iPhone.**
   Open VitaPlay and try CarPlay normally for ~20 seconds.
   **Do not press L+R yet**. This phase uses v7 video/session code and
   keeps the new SBC capture/decoder inactive.
5. If CarPlay video comes up, then hold **both shoulder buttons L+R**
   once to start the SBC receiver, select PlayStation Vita for iPhone
   Bluetooth audio, and try playing music.
6. If CarPlay still cannot connect at step 4, you may press L+R to
   check whether standalone Bluetooth audio can still be decoded.
   Do not interpret this as a successful CarPlay test.
7. Retrieve `ux0:data/VitaPlay/log.txt` and
   `ux0:data/VitaPlay/l2capprobe.log`.

## Expected events

- `v11 V7 VIDEO TEST: Bluetooth SBC capture OFF until L+R is pressed`
  means Bluetooth music capture is disabled on boot
- `CarPlay session active`, `stream type=110`, or a working CarPlay
  image means the **new** plugin is not preventing video
- `v11 MANUAL SBC ENABLE` followed by
  `v10 kernel Bluetooth SBC capture enabled=1` means the user
  manually switched on the in-app decoder and kernel RTP queue
- `v10 BT RTP` in the L2CAP log distinguishes accepted SBC RTP from
  incomplete/rejected packets and upstream RTP sequence gaps
- `v9 BT SOUND` in the VitaPlay log shows PCM playback and underruns

## Interpretation

- Video fails **before** L+R: modifying the Bluetooth plugin is a strong
  suspect, but also confirm the iPhone/Wi-Fi routing and retry the same
  configuration with original v7 + original plugin.
- Video works before L+R, breaks afterward: the audio capture/decoder
  is likely interfering with CarPlay.
- Video remains stable, audio starts, but remains choppy: next fix is
  in the A2DP packet path, not the H.264 decoder.

## Rollback

Restore the original v7 `vitaplaybt.skprx`, reboot, reinstall the v7
VPK, leave user pairing/MFi data untouched.

SBC: Google libsbc (Apache-2.0), revision
`6e505650145c9973d08a0bdd5e5f5e1914305e40`.
