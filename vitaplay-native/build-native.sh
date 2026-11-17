#!/usr/bin/env bash
set -euo pipefail
# VitaPlay v12 builds from editable, committed native C sources.
# No v5-v11 patch scripts, no patching existing VPKs, no private MFi keys.
git clone --depth 1 https://github.com/ironmanx75/vitaplay-release.git upstream
git -C upstream checkout --detach cb0d0dcb7e952d8e5c068843fe4917c3a31ebf7c
mkdir -p upstream/vitaplay/vendor
git clone --depth 1 https://github.com/google/libsbc.git upstream/vitaplay/vendor/libsbc
git -C upstream/vitaplay/vendor/libsbc checkout --detach 6e505650145c9973d08a0bdd5e5f5e1914305e40
# All edited C/H files are SOURCE files committed in vitaplay-native/
cp vitaplay-native/Makefile upstream/vitaplay/Makefile
cp vitaplay-native/src/*.[ch] upstream/vitaplay/src/
cp vitaplay-native/btplugin/Makefile upstream/vitaplay/btplugin/Makefile
cp vitaplay-native/btplugin/vitaplaybt.c upstream/vitaplay/btplugin/vitaplaybt.c
cp vitaplay-native/btplugin/exports.yml upstream/vitaplay/btplugin/exports.yml
# Exact source assertions before compiling
grep -q 'v12 Bluetooth PAN guard requested' upstream/vitaplay/src/main.c
grep -q 'v12 PAN GUARD reject BNEP' upstream/vitaplay/btplugin/vitaplaybt.c
grep -q 'HYBRID_PLC_BURST_MAX' upstream/vitaplay/src/btaudio.c
grep -q 'vpbt_pan_guard' upstream/vitaplay/btplugin/exports.yml
make -C upstream/vitaplay -j1 VITASDK="${VITASDK}"
mkdir -p out
python3 - <<'PY'
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import hashlib,sys
vpk=Path("upstream/vitaplay/VitaPlay.vpk")
plugin=Path("upstream/vitaplay/btplugin/vitaplaybt.skprx").read_bytes()
elf=Path("upstream/vitaplay/btplugin/build/vitaplaybt.elf").read_bytes()
userelf=Path("upstream/vitaplay/build/VitaPlay.elf").read_bytes()
assert b"v12 PAN GUARD reject BNEP" in elf
assert b"vpbt_pan_guard" in elf
assert b"v12 Bluetooth PAN guard requested" in userelf
assert b"PLCskipped" in userelf
assert b"v11 MANUAL SBC ENABLE" in userelf
outvpk=Path("out/VitaPlay-v12-Native-Hybrid-PANGuard-SBC.vpk")
outvpk.write_bytes(vpk.read_bytes())
with ZipFile(outvpk) as z:
    assert z.testzip() is None
    assert z.read("vitaplaybt.skprx")==plugin
    assert b"v12 Bluetooth PAN guard requested" in z.read("eboot.bin")
outzip=Path("out/VitaPlay-v12-Native-Hybrid.zip")
with ZipFile(outzip,"w",ZIP_DEFLATED) as z:
    z.write(outvpk,outvpk.name)
    z.writestr("vitaplaybt.skprx",plugin)
    z.write("vitaplay-native/README-v12.md","README.md")
    z.write("upstream/vitaplay/vendor/libsbc/LICENSE","Google-libsbc-LICENSE")
    for path in sorted(Path("vitaplay-native/src").glob("*")):
        if path.suffix in (".c",".h"): z.write(path,"Source/"+path.relative_to("vitaplay-native").as_posix())
    for rel in ("Makefile","btplugin/Makefile","btplugin/exports.yml","btplugin/vitaplaybt.c"):
        z.write(Path("vitaplay-native")/rel,"Source/"+rel)
with ZipFile(outzip) as z: assert z.testzip() is None
print("NATIVE v12 VPK:",outvpk,outvpk.stat().st_size,"sha256",hashlib.sha256(outvpk.read_bytes()).hexdigest())
print("NATIVE v12 ZIP:",outzip,outzip.stat().st_size,"plugin_sha256",hashlib.sha256(plugin).hexdigest())
PY
