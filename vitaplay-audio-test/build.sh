#!/usr/bin/env bash
set -euo pipefail

# Source-only test: no MFi private identity is used or bundled.
git clone --depth 1 https://github.com/ironmanx75/vitaplay-release.git upstream
cp vitaplay-audio-test/ntp.c upstream/vitaplay/src/ntp.c

# The official full VitaSDK image includes the target libraries.
make -C upstream/vitaplay -j2 VITASDK="${VITASDK}"

# Keep the resident kernel plugin IDENTICAL to the original release.
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
dest = Path("out/VitaPlay-audio-clock-test.vpk")
with ZipFile(original) as a, ZipFile(built) as b:
    original_plugin = a.read("vitaplaybt.skprx")
    original_exe = a.read("eboot.bin")
    updated_exe = b.read("eboot.bin")
    if original_exe == updated_exe:
        sys.exit("ERROR: compiled eboot did not change")
    if b"timing PT211 synchronized" not in updated_exe:
        sys.exit("ERROR: timing diagnostics missing from new eboot")
    with ZipFile(dest, "w") as out:
        for item in b.infolist():
            payload = original_plugin if item.filename == "vitaplaybt.skprx" else b.read(item.filename)
            out.writestr(item, payload)
with ZipFile(dest) as out:
    if out.testzip() is not None:
        sys.exit("ERROR: VPK failed ZIP integrity")
    if out.read("vitaplaybt.skprx") != original_plugin:
        sys.exit("ERROR: Bluetooth kernel plugin changed")
print("VPK:", dest, dest.stat().st_size)
print("Original plugin SHA256:", hashlib.sha256(original_plugin).hexdigest())
print("Compiled eboot SHA256:", hashlib.sha256(updated_exe).hexdigest())
PY
