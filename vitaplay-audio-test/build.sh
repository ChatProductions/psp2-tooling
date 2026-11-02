#!/usr/bin/env bash
set -euo pipefail

# Source-only audio codec negotiation test: no MFi private identity is used or bundled.
git clone --depth 1 https://github.com/ironmanx75/vitaplay-release.git upstream
cp vitaplay-audio-test/ntp.c upstream/vitaplay/src/ntp.c

# Change the on-wire audio advertisement in SOURCE, never in ARM instructions.
# The iPhone on iOS 18 has not sent an audio SETUP in any of the submitted logs.
# DiPlay's 48 kHz PCM masks are narrower than VitaPlay's multi-rate masks.
# VitaPlay currently cannot decode the advertised stream-102 AAC codec, so remove
# this unsupported offer and steer the phone toward its implemented PCM/Opus path.
python3 - <<'PY'
from pathlib import Path
path=Path("upstream/vitaplay/src/info.c")
source=path.read_text()
replacements={
    "#define AUDIO_PCM 0xcffc":"#define AUDIO_PCM 0xc3fc",
    "#define AUDIO_PCM_MONO 0x4554":"#define AUDIO_PCM_MONO 0x4154",
    '    pl_push(a, latency(102, "default"));':'    /* No stream 102: AAC decoding is not implemented on Vita. */',
    '    pl_push(a, format(102, "media", AUDIO_AAC_LC));':'    /* Prefer stream 100 PCM media. */',
}
for before, after in replacements.items():
    count=source.count(before)
    if count != 1:
        raise SystemExit(f"Expected precisely one match for {before!r}, found {count}")
    source=source.replace(before,after)
if "#define AUDIO_PCM 0xc3fc" not in source or "#define AUDIO_PCM_MONO 0x4154" not in source:
    raise SystemExit("PCM48 source patch missing")
assert 'format(100, "media", AUDIO_PCM)' in source
assert 'format(100, "default", AUDIO_PCM | AUDIO_OPUS)' in source
path.write_text(source)
print("VitaPlay /info formats: DiPlay 48 kHz PCM stereo + mono; stream-102 AAC advertisement disabled.")
PY


# The official full VitaSDK image includes the target libraries.
make -C upstream/vitaplay -j1 VITASDK="${VITASDK}"

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
dest = Path("out/VitaPlay-audio-v3-PCM48.vpk")
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
