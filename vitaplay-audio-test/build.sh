#!/usr/bin/env bash
set -euo pipefail

# Source-only audio codec negotiation test: no MFi private identity is used or bundled.
git clone --depth 1 https://github.com/ironmanx75/vitaplay-release.git upstream
# Freeze known-working upstream source at the v7 code baseline.
git -C upstream checkout --detach cb0d0dcb7e952d8e5c068843fe4917c3a31ebf7c
cp vitaplay-audio-test/ntp.c upstream/vitaplay/src/ntp.c

# Restore AAC-LC media output and decode with the Vita native SceAudiodec API.
python3 vitaplay-audio-test/aac_patch.py
# Diagnostic-only: distinguish TCP close vs socket error on AirPlay control/event.
python3 vitaplay-audio-test/disconnect_diag.py
# v7: Wi-Fi audio setup-capability negotiation and event command dispatch.
python3 vitaplay-audio-test/wifi_audio_v7.py
# v8: honor Wi-Fi CarPlay disableBluetooth using tracked iAP2 kernel peer.
python3 vitaplay-audio-test/wifi_bluetooth_handoff_v8.py

# The official full VitaSDK image includes the target libraries.
make -C upstream/vitaplay -j1 VITASDK="${VITASDK}"

# Verify the v8 Bluetooth kernel plugin differs from the original; installation
# requires copying it to the active taiHEN path and rebooting.
# This build is experimental and targets the same Vita firmware 3.65.
curl --fail --location --silent --show-error \
  https://raw.githubusercontent.com/ironmanx75/vitaplay-release/main/VitaPlay-beta.vpk \
  -o /tmp/VitaPlay-original.vpk

mkdir -p out
python3 - <<'PY'
from pathlib import Path
from zipfile import ZipFile
import hashlib
import sys

original = Path("/tmp/VitaPlay-original.vpk")
built = Path("upstream/vitaplay/VitaPlay.vpk")
dest = Path("out/VitaPlay-v8-wifi-audio-handoff.vpk")
with ZipFile(original) as a, ZipFile(built) as b:
    original_plugin = a.read("vitaplaybt.skprx")
    updated_plugin = b.read("vitaplaybt.skprx")
    original_exe = a.read("eboot.bin")
    if updated_plugin == original_plugin:
        sys.exit("ERROR: v8 kernel plugin identical to original; handoff was not compiled")
    kernel_elf = Path("upstream/vitaplay/btplugin/build/vitaplaybt.elf").read_bytes()
    if b"v8 Wi-Fi handoff" not in kernel_elf:
        sys.exit("ERROR: handoff code missing from compiled kernel ELF")
    if b"vpbt_handoff_disconnect" not in kernel_elf:
        sys.exit("ERROR: kernel export missing from compiled ELF")
    updated_exe = b.read("eboot.bin")
    if original_exe == updated_exe:
        sys.exit("ERROR: compiled eboot did not change")
    if b"timing PT211 synchronized" not in updated_exe:
        sys.exit("ERROR: timing diagnostics missing from new eboot")
    if b"native AAC decoder ready" not in updated_exe:
        sys.exit("ERROR: native AAC decoder diagnostic not present in eboot")
    if b"VitaPlay starting - v8 Wi-Fi audio Bluetooth handoff test" not in updated_exe:
        sys.exit("ERROR: v8 startup marker missing from eboot")
    if b"v6 control TCP peer EOF" not in updated_exe:
        sys.exit("ERROR: v6 disconnect diagnostics absent from eboot")
    if b"v7 initial SETUP proposed features" not in updated_exe:
        sys.exit("ERROR: v7 AirPlay feature negotiation was not compiled")
    if b"v7 event channel POST /command" not in updated_exe:
        sys.exit("ERROR: v7 event-channel command handling was not compiled")
    if b"v7 stream lifecycle command" not in updated_exe:
        sys.exit("ERROR: v7 stream lifecycle logging was not compiled")
    if b"CarPlay session active - v7 Wi-Fi audio negotiation diagnostic" not in updated_exe:
        sys.exit("ERROR: compiled binary lacks v7 RECORD marker")
    if b"v8 Wi-Fi handoff requested" not in updated_exe:
        sys.exit("ERROR: v8 handoff integration absent from eboot")
    if b"v8 HANDOFF NOT ATTEMPTED" not in updated_exe:
        sys.exit("ERROR: v8 plugin API capability check absent")
    with ZipFile(dest, "w") as out:
        for item in b.infolist():
            payload = b.read(item.filename)
            out.writestr(item, payload)
with ZipFile(dest) as out:
    if out.testzip() is not None:
        sys.exit("ERROR: VPK failed ZIP integrity")
    if out.read("vitaplaybt.skprx") != updated_plugin:
        sys.exit("ERROR: rebuilt Bluetooth plugin not included")
    if out.read("vitaplaybt.skprx") == original_plugin:
        sys.exit("ERROR: packaged old Bluetooth plugin")
print("VPK:", dest, dest.stat().st_size)
print("Original plugin SHA256:", hashlib.sha256(original_plugin).hexdigest())
print("v8 plugin SHA256:", hashlib.sha256(updated_plugin).hexdigest())
print("Compiled eboot SHA256:", hashlib.sha256(updated_exe).hexdigest())
PY
