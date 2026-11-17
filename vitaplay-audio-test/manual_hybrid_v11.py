#!/usr/bin/env python3
"""v11 minimal A/B integration: v7 CarPlay logic, v10 plugin, optional SBC.

Start with *exactly* the v7 video/session logic. The v10 media reader
is present but gated OFF until the user presses both shoulder buttons.
Does not auto-enable during CarPlay startup or mutate AirPlay negotiation.
This makes video compatibility measurable independently of audio playback.
"""
from pathlib import Path
root=Path("upstream/vitaplay")
def one(path,before,after):
    p=root/path;s=p.read_text();n=s.count(before)
    if n!=1: raise SystemExit(f"v11 {path}: expected exactly one source anchor, got {n} for {before[:80]!r}")
    p.write_text(s.replace(before,after));print("v11 patched",path)

main="src/main.c"
one(main,
    """        if (state == SESSION_STREAMING) {
            if (!hybrid_audio_started) {
                hybrid_audio_started = true;
                vp_log("v10 VIDEO STREAMING: enable in-app Bluetooth SBC receiver now");
                bt_audio_start();
            }
""",
    """        if (state == SESSION_STREAMING) {
""")
one(main,
    '        if (pressed & SCE_CTRL_SELECT) overlay = !overlay;',
    '        if (pressed & SCE_CTRL_SELECT) overlay = !overlay;\n'
    '        /* v11: userspace never calls SBC capture until a deliberate test.\n'
    '         * Hold both shoulder buttons to enable it exactly once.\n'
    '         * This keeps original v7 Wi-Fi CarPlay initialization isolated\n'
    '         * from the merged-kernel SBC queue and speaker decoding.\n'
    '         */\n'
    '        const uint32_t shoulders = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;\n'
    '        if (!hybrid_audio_started && (pad.buttons & shoulders) == shoulders &&\n'
    '            (pressed & shoulders)) {\n'
    '            hybrid_audio_started = true;\n'
    '            vp_log("v11 MANUAL SBC ENABLE: state=%d (do not confuse this with CarPlay readiness)",\n'
    '                   (int)session_current_state());\n'
    '            bt_audio_start();\n'
    '        }')
one(main,
    '    draw_text(30, y, COLOR_DIM, "SELECT: toggle this panel   START+SELECT: quit");',
    '    draw_text(30, y, COLOR_DIM, "SELECT: panel  L+R: enable BT audio  START+SELECT: quit");')
one(main,
    "VitaPlay starting - v10 VIDEO-FIRST CarPlay + Bluetooth SBC diagnostics",
    "VitaPlay starting - v11 V7 video baseline with manual v10 Bluetooth SBC plugin")
one(main,
    '    vp_log("v10 VIDEO-FIRST: Bluetooth SBC capture disabled until CarPlay video reaches STREAMING");',
    '    vp_log("v11 V7 VIDEO TEST: Bluetooth SBC capture OFF until L+R is pressed");')
source=(root/main).read_text()
assert "v11 MANUAL SBC ENABLE" in source
assert "v10 VIDEO STREAMING: enable" not in source
assert "SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER" in source
print("PASS v11: v7 CarPlay sessions unmodified, SBC manually gated, v10 kernel packet diagnostics kept")
