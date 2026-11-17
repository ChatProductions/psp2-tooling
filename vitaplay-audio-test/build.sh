#!/usr/bin/env bash
set -euo pipefail

# Source-only audio codec negotiation test: no MFi private identity is used or bundled.
git clone --depth 1 https://github.com/ironmanx75/vitaplay-release.git upstream
git -C upstream checkout --detach cb0d0dcb7e952d8e5c068843fe4917c3a31ebf7c
cp vitaplay-audio-test/ntp.c upstream/vitaplay/src/ntp.c

# Restore AAC-LC media output and decode with the Vita native SceAudiodec API.
python3 vitaplay-audio-test/aac_patch.py
# Diagnostic-only: distinguish TCP close vs socket error on AirPlay control/event.
python3 vitaplay-audio-test/disconnect_diag.py
# v7: Wi-Fi audio setup-capability negotiation and event command dispatch.
python3 vitaplay-audio-test/wifi_audio_v7.py
# v9: merge A2DP SBC reception into existing vitaplaybt.skprx and userspace.
mkdir -p upstream/vitaplay/vendor
git clone --depth 1 https://github.com/google/libsbc.git upstream/vitaplay/vendor/libsbc
git -C upstream/vitaplay/vendor/libsbc fetch --depth 1 origin 6e505650145c9973d08a0bdd5e5f5e1914305e40
git -C upstream/vitaplay/vendor/libsbc checkout --detach 6e505650145c9973d08a0bdd5e5f5e1914305e40
python3 vitaplay-audio-test/hybrid_v9.py

# The official full VitaSDK image includes the target libraries.
make -C upstream/vitaplay -j1 VITASDK="${VITASDK}"

# Build/package matching modified kernel plugin from source. Do not restore original.
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
dest = Path("out/VitaPlay-v9-BluetoothAudio-WiFiVideo.vpk")
with ZipFile(original) as a, ZipFile(built) as b:
    original_plugin = a.read("vitaplaybt.skprx")
    updated_plugin = b.read("vitaplaybt.skprx")
    original_exe = a.read("eboot.bin")
    if original_plugin == updated_plugin:
        sys.exit("ERROR: v9 kernel Bluetooth+audio hook was not rebuilt")
    elf = Path("upstream/vitaplay/btplugin/build/vitaplaybt.elf").read_bytes()
    if b"vpbt_audio_read" not in elf or b"v9 hybrid SBC RTP" not in elf:
        sys.exit("ERROR: missing compiled hybrid SBC plugin exports")
    updated_exe = b.read("eboot.bin")
    if original_exe == updated_exe:
        sys.exit("ERROR: compiled eboot did not change")
    if b"timing PT211 synchronized" not in updated_exe:
        sys.exit("ERROR: timing diagnostics missing from new eboot")
    if b"native AAC decoder ready" not in updated_exe:
        sys.exit("ERROR: native AAC decoder diagnostic not present in eboot")
    if b"v9 Bluetooth audio plus Wi-Fi CarPlay video" not in updated_exe:
        sys.exit("ERROR: missing v9 hybrid startup marker")
    if b"v9 HYBRID PLAYBACK START" not in updated_exe:
        sys.exit("ERROR: missing integrated Bluetooth SBC speaker path")
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
    with ZipFile(dest, "w") as out:
        for item in b.infolist():
            payload = b.read(item.filename)
            out.writestr(item, payload)
with ZipFile(dest) as out:
    if out.testzip() is not None:
        sys.exit("ERROR: VPK failed ZIP integrity")
    if out.read("vitaplaybt.skprx") != updated_plugin:
        sys.exit("ERROR: VPK missing matching Bluetooth SBC kernel plugin")
    if out.read("vitaplaybt.skprx") == original_plugin:
        sys.exit("ERROR: VPK still contains old kernel plugin")
print("VPK:", dest, dest.stat().st_size)
print("Original plugin SHA256:", hashlib.sha256(original_plugin).hexdigest())
print("Hybrid plugin SHA256:", hashlib.sha256(updated_plugin).hexdigest())
print("Compiled eboot SHA256:", hashlib.sha256(updated_exe).hexdigest())
PY
