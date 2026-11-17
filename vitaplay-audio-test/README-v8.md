# VitaPlay v8 — experimental native Wi-Fi CarPlay audio handoff

**This is an experimental protocol test.** The VitaPlay video pipeline is
unchanged; v8 changes the *Bluetooth-to-Wi-Fi handoff* after AirPlay starts.

## What your v7 log established

- The iPhone successfully authenticated over Bluetooth and then opened a
  working Wi-Fi AirPlay session and stream 110 (960×544 video).
- A `disableBluetooth` command arrived at about 17 seconds, but VitaPlay did
  not act on it. The iPhone still listed PlayStation Vita as a Bluetooth audio
  destination in the screenshot.
- The iPhone did not send SETUP for audio streams 100 / 101 / 102, even when
  audio resource 2 changed to entity 1.
- Apple WWDC17 wireless CarPlay documentation instructs the head unit to
  disconnect Bluetooth connections to the device after `disableBluetooth`.

## v8 changes

- **New matching `vitaplaybt.skprx` kernel plugin** exports a guarded
  Bluetooth handoff function and a plugin-version check.
- Upon **explicit** iPhone `disableBluetooth` only, VitaPlay requests
  `ksceBtStartDisconnect` for the connected iAP2 peer recorded during
  RFCOMM setup. It does NOT disable the Vita Bluetooth radio or
  intentionally disconnect other Bluetooth devices.
- Logs the request/result in `ux0:data/VitaPlay/log.txt` and actual
  Bluetooth disconnect event in `ux0:data/VitaPlay/l2capprobe.log`.
- Keeps the v7 44.1/48-kHz PCM/AAC offers, event command logging, native
  AAC decoder, video code, mDNS advertisement and timing diagnostics.
- All source builds pin upstream VitaPlay to
  `cb0d0dcb7e952d8e5c068843fe4917c3a31ebf7c`.
- No changes to or inclusion of private MFi credentials.

**Audio is NOT confirmed working.** Disconnecting Bluetooth may expose a
second missing step: the Wi-Fi iAP2 tunnel (stream 130) is not observed in
the v7 session. If the phone closes CarPlay after the disconnection, this
test still tells us which component must be handled next.

## Install

You need a jailbroken PS Vita on **3.65**, and a known working recovery
process for faulty taiHEN kernel plugins.

1. Back up the **active** taiHEN `config.txt`. Confirm whether your
   setup uses `ur0:tai/config.txt` or `ux0:tai/config.txt`.
2. Using VitaShell copy the new `vitaplaybt.skprx` to the path referenced
   by the **existing** `*KERNEL` VitaPlay plugin entry (often
   `ur0:tai/vitaplaybt.skprx`). Save your original plugin as a backup.
3. Ensure the standalone `a2dp_probe.skprx` plugin is disabled and that
   exactly ONE VitaPlay Bluetooth plugin is loaded.
4. Fully reboot the Vita to load the new kernel plugin.
5. Install `VitaPlay-v8-wifi-audio-handoff.vpk` over your existing
   VitaPlay VPK, without deleting VitaPlay's data files or MFi identity.
6. Connect the iPhone to CarPlay as usual. After video appears, play music
   and test whether the Vita speakers produce sound.
7. Export **both** logs:
   - `ux0:data/VitaPlay/log.txt`
   - `ux0:data/VitaPlay/l2capprobe.log`

## Expected indicators

App log:
`v8 disableBluetooth received; plugin API=0x56504238`
and
`v8 Wi-Fi handoff requested: disconnect iAP2 peer rc=0x...`.

Plugin log:
`v8 Wi-Fi handoff SceBtStartDisconnect...`
then ideally
`v8 Wi-Fi handoff confirmed: iAP2 Bluetooth peer disconnected`.

A further breakthrough would be `SETUP audio type=100`,
`SETUP audio type=101` or `SETUP audio type=102` in the app log.
If instead CarPlay closes, check when `control TCP peer EOF` appears.

## Rollback

If CarPlay no longer connects, restore the original
`vitaplaybt.skprx` in the active `*KERNEL` plugin path, reboot, and
reinstall your prior working VitaPlay VPK (v7). Do not remove your
pairing / MFi data. Kernel plugins can prevent normal boot, so have
your plugin-bypass/recovery method ready before installing.

## Sources

- Apple, **Developing Wireless CarPlay Systems**, WWDC 2017 session 717:
  https://developer.apple.com/videos/play/wwdc2017/717/
- VitaSDK `ksceBtStartDisconnect(mac0, mac1)`:
  https://docs.vitasdk.org/group__SceBtKernel.html
