# VitaPlay v12 — native source release / experimental hybrid

VitaPlay v12 is compiled **directly from edited C source** in
`vitaplay-native/src/`, the Bluetooth kernel plugin source
`vitaplay-native/btplugin/vitaplaybt.c`, and matching Makefiles/exports.
No old VPK is patched, and no v5-v11 stack of source patch scripts runs.

## What v11 confirmed

- CarPlay H.264 stream 110 successfully displayed 960×544 on Vita while
  Bluetooth music reached the speakers after manual **L+R**.
- The initial iAP2 `carplay-start` was sent at 15.075 s, but the Wi-Fi
  AirPlay connection arrived at 54.857 s: ~39.8 seconds late.
- The iPhone simultaneously created a **Bluetooth PAN / Ethernet** interface,
  with a 169.254.33.156 self-assigned address and no router. Re-selecting
  Wi-Fi apparently restored the CarPlay session.
- After Bluetooth music began, log reported 4323 accepted SBC RTP packets
  and 2617 missing RTP sequence positions (37.7% of expected positions);
  268 underruns, 2720 silent 23ms blocks by the final log.
- Kernel trace saw 3321 sequence positions missing BEFORE its audio queue;
  `filter_reject=0`. At the end, 1329 queue drops accrued after the app
  stopped reading the resident plugin. Missing incoming packets cannot be
  repaired by a newer SBC decoder alone.

## Changes

1. **Bluetooth PAN guard**: native `vitaplaybt.skprx` now optionally
   rejects incoming BNEP/PAN L2CAP requests (PSM 0x000f) on the same iPhone
   connection as iAP2. It sends an explicit standard L2CAP negative
   connection response in a worker thread, never from the RX hook.
   iAP2 (RFCOMM PSM=3) and AVDTP/A2DP (PSM=0x0019) are not blocked.
   The guard is **enabled by default** in VitaPlay and can be disabled
   by adding `pan_guard=0` to `ux0:data/VitaPlay/config.txt`
   and restarting the application. This is experimental.
2. **SBC packet-loss concealment**: the in-app decoder now fills up to
   **four** missing 23ms media slots per RTP sequence gap with native
   `sbc_decode(NULL)` zero-subband synthesis, which reduces hard clicks
   and sudden silence at some gaps. This is NOT recovery of missing
   samples and cannot guarantee smooth music when the underlying radio
   is dropping 38% of RTP positions. Late/duplicate packets are ignored.
   Logs show `PLC` and `PLCskipped` counts.
3. **Native code only**: app and plugin source are committed under the
   same `vitaplay-native/` tree. The build script copies these edited
   files into the untouched upstream project as ordinary source files,
   builds with VitaSDK, and packages VPK + matching SKPRX + source.
4. **Video-first with manual audio toggle retained**: L+R starts Bluetooth
   SBC reception. Leave it off during initial CarPlay connection for a
   clean split between networking and audio diagnostics.
5. **No automatic Bluetooth disconnect** from v8.

## Installation / test

Kernel hooks can prevent boot. Back up the active
`ur0:tai/config.txt` (or whichever config your Vita uses), your
previous kernel plugin, and your known-working v11 VPK. Have the
kernel-plugin recovery procedure ready.

1. Copy the ZIP's `vitaplaybt.skprx` to **the same path already
   referenced** under your active `*KERNEL` VitaPlay plugin entry.
   Keep `a2dp_probe.skprx` and any other separate SceBt RX hooks
   disabled. Reboot.
2. Install `VitaPlay-v12-Native-Hybrid-PANGuard-SBC.vpk` over VitaPlay;
   **do not** remove your MFi/identity/pairing data.
3. Start with music stopped. Ensure the iPhone is connected to
   `FiberHGW_ZTN3KD` Wi-Fi and Bluetooth on. Reconnect the iPhone
   Bluetooth session if it was left up from a previous test; existing
   PAN sessions cannot be undone by a new connection reject.
4. Connect wireless CarPlay. Verify the screen arrives. The plugin log
   should contain `v12 PAN GUARD reject BNEP PSM=0x000f` if the phone
   requested Bluetooth PAN on this fresh connection.
5. Only after video works, press **L+R** once. Select PlayStation Vita
   in the iPhone's music output selector and start music.
6. Export logs:
   `ux0:data/VitaPlay/log.txt` and
   `ux0:data/VitaPlay/l2capprobe.log`.

## Success/failure interpretation

- If CarPlay arrives immediately and PAN is refused, the competing PAN
  route was plausibly blocking video setup. Look for disappearance of
  Settings -> Ethernet -> Bluetooth PAN on the iPhone. An already-open
  PAN may persist; reconnect Bluetooth to retest.
- If video fails or the phone no longer pairs, **disable PAN guard**
  with `pan_guard=0` (restart VitaPlay) or restore the known-working
  v11 plugin/VPK. This is not a proven universal protocol fix.
- `v9 HYBRID PLAYBACK START` indicates audio port opened;
  `PLC=` indicates that missing RTP is being concealed.
  `prequeue_missing=` remains the key measurement of incoming loss.
  If losses persist at the same rate, next work must target Bluetooth
  coexistence/transport scheduling rather than SBC decoding.

## Rollback

Restore the original v11 `vitaplaybt.skprx`, reboot, then reinstall
v11 VPK. Restore config if changed. No pairing data need be deleted.

## Open source / provenance

- Upstream source: https://github.com/ironmanx75/vitaplay-release
  pinned `cb0d0dcb7e952d8e5c068843fe4917c3a31ebf7c`
- SBC decoder: https://github.com/google/libsbc
  pinned `6e505650145c9973d08a0bdd5e5f5e1914305e40`
  (Apache-2.0)
- Dev branch: `ChatProductions/psp2-tooling`,
  `experiment/vitaplay-native-hybrid-v12-20261117`.
