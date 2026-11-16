# Vita A2DP passive probe — firmware 3.65 only

Purpose: determine whether an iPhone actually establishes an AVDTP/A2DP
channel and sends data when "PlayStation Vita" is selected as an audio output,
**with the VitaPlay app closed**.

This is **not an audio receiver**. It does not advertise A2DP sink, send
Bluetooth messages, or decode/play music. It records short packet headers
and aggregate counts, never complete audio payloads.

## Compatibility and safety

The plugin uses the existing 3.65 SceBt RX hook at segment 0 + 0x6558.
It refuses to hook unless SceBt module NID is 0x67e0c2eb.
It is compiled but **not yet hardware-tested**. A kernel hook can crash the Vita.

**Back up the active taiHEN config**. If ux0:tai/config.txt exists and is
active, edit that; otherwise edit ur0:tai/config.txt.
Temporarily **disable** ur0:tai/vitaplaybt.skprx under *KERNEL for the
standalone test. Do not run two kernel Bluetooth hooks simultaneously.
Know your recovery procedure before using an experimental kernel plugin:
many Ensō/taiHEN setups support holding L at boot to skip plugins.

## Install and test

1. Copy a2dp_probe.skprx to ur0:tai/a2dp_probe.skprx using VitaShell.
2. Add ur0:tai/a2dp_probe.skprx under *KERNEL in the active config.
3. Reboot. Leave VitaPlay closed.
4. Pair/connect the iPhone through Vita Settings > Devices > Bluetooth.
5. On iPhone, select PlayStation Vita in the AirPlay/audio output selector.
6. Play music for 20–30 seconds.
7. Transfer ux0:data/VitaPlay/a2dp_probe.log from Vita to PC.

## Log interpretation

- SIG CONNECT_REQ ... PSM=0x0019 [AVDTP] indicates an inbound AVDTP request.
- SIG CONFIG_REQ / CONFIG_RSP indicates L2CAP channel setup.
- DATA cid=... records only the first two payload bytes per selected packet.
- RATE cid=... reports packet counts over five-second windows.

A high packet rate on a confirmed AVDTP **media** channel suggests audio
data, but proves neither successful AVDTP negotiation nor audio playback.
This hook observes incoming packets only and does not record outbound TX.

## Uninstall

Remove ur0:tai/a2dp_probe.skprx from *KERNEL, restore the prior
vitaplaybt.skprx line, and reboot. The CarPlay VPK/MFi files remain unchanged.
