# VitaPlay v10 — video-first hybrid test + Bluetooth RTP loss diagnostics

**Purpose:** isolate why v9 Bluetooth sound was choppy and its CarPlay
screen never initialized. In the v9 hardware log there were 4,358
successfully decoded SBC packets but 1,534 missing RTP sequence positions
(~26%), 201 underruns and 1,535 silence blocks. There were zero **new**
kernel ring drops during app playback. The iPhone authenticated through
iAP2 and VitaPlay sent carplay-start twice, but no RTSP connection arrived
and stream 110 (video) never opened.

The previous v0.6 standalone log also showed considerable RTP sequence
loss, despite successful decoding. Google's libsbc pinned commit is the
current upstream main commit as of Nov 17, 2026 (2022-era codec source).
Simply updating the SBC decoder cannot fix missing incoming audio.

## Changed in v10

- **Prioritize video**: the kernel SBC capture starts disabled. VitaPlay first
  completes the original Bluetooth iAP2/MFi handshake and waits for normal
  Wi-Fi CarPlay *SESSION_STREAMING*. Only then does the app enable kernel
  SBC reception, SBC decoding and the BGM speaker port. This avoids playing
  Bluetooth music while CarPlay is attempting to initialize.
- The one resident `vitaplaybt.skprx` hook handles both iAP2 and A2DP,
  but the A2DP branch immediately exits unless video is already streaming.
- Adds **pre-SBC-filter** packet diagnostics on the Bluetooth RX hook:
  `cand`=candidate RTP/PT96 packets; `incomplete`=truncated L2CAP or
  unsupported size; `filter_reject`=failed SBC sync/packet count;
  `prequeue_missing`=sequence gaps observed by the kernel *before* the
  queue; `duplicates`=repeated RTP sequence number.
- `queue_drops` is a separate, cumulative ring overflow count.
- No v8 forced Bluetooth disconnection; Wi-Fi video code is unchanged.

## How to test this version

**Back up the current kernel plugin, the active taiHEN config and working v7
VPK. The new kernel hook is experimental and hardware-unverified.**

1. Extract the ZIP. Copy `vitaplaybt.skprx` to the path already enabled
   under `*KERNEL`, often `ur0:tai/vitaplaybt.skprx`.
   Make sure standalone `a2dp_probe.skprx` is disabled.
2. Reboot Vita to load the new kernel plugin. Install the enclosed v10 VPK.
3. **Stop music on the iPhone and choose iPhone as audio output before
   connecting.** This is essential for distinguishing CarPlay initialization
   from the Bluetooth music route.
4. Open VitaPlay and connect wireless CarPlay; wait for the CarPlay display.
5. **Only after video appears**, start music and select **PlayStation Vita**
   as audio output in iOS. Leave Bluetooth connected. Listen for audio.
6. Export `ux0:data/VitaPlay/log.txt` and
   `ux0:data/VitaPlay/l2capprobe.log`.

## Interpreting logs

- `v10 VIDEO-FIRST`: plugin's A2DP capture is disabled during setup.
- `CarPlay session active`, stream 110, or `v10 VIDEO STREAMING`:
  CarPlay display established, now Bluetooth receiver enabled.
- `v10 BT RTP`: reports both kernel RTP candidate/format counters and
  accepted packet totals.
- `prequeue_missing` rises but `queue_drops` does not:
  incoming RTP stream is already discontinuous at the interception point.
  Could mean radio/BT stack loss, *or* packets skipped by our filter.
- `filter_reject` rises: our narrow SBC media parser may be discarding
  legitimate audio; inspect format before changing Bluetooth stack.
- `v9 HYBRID PLAYBACK START` and `v9 BT SOUND`: inherited native
  decoder/44.1-kHz audio port started.
- No `v10 VIDEO STREAMING`: CarPlay/RTSP handshake remains blocked
  before Bluetooth capture starts. Do not infer this is an audio decoder bug.

This build is a **controlled hardware diagnostic**, not yet proof that
iOS will send Bluetooth media simultaneously with CarPlay video. If
CarPlay fails even when starting with iPhone audio output and the SBC
receiver disabled, compare to the last confirmed working v7 build.

## Rollback

Restore the previous `vitaplaybt.skprx` in the active `*KERNEL` path,
reboot, and reinstall the working VitaPlay v7 VPK. Do not delete
pairing/MFi identity data. Know how to bypass/recover a crashing kernel
plugin on your own Vita setup before testing this one.

## Open-source decoder

Google libsbc, Apache-2.0, pinned commit
`6e505650145c9973d08a0bdd5e5f5e1914305e40`.
Source: https://github.com/google/libsbc
