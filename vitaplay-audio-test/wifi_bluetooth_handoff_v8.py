#!/usr/bin/env python3
"""VitaPlay v8: honor the iPhone's explicit disableBluetooth handoff.

Patches upstream source after the already-tested v5/v6/v7 scripts.
Only the connected iAP2 Bluetooth peer is disconnected using the stock
SceBt ksceBtStartDisconnect API. The Bluetooth radio is NOT disabled.
No MFi private key, pairing material, video code or AirPlay transport changes.
"""
from pathlib import Path

root = Path("upstream/vitaplay")

def one(path, before, after):
    p = root / path
    s = p.read_text()
    n = s.count(before)
    if n != 1:
        raise SystemExit(f"v8 patch {path}: expected one anchor, got {n}: {before[:90]!r}")
    p.write_text(s.replace(before, after))
    print("v8 patched", path, repr(before[:70]))

# Kernel side: save exactly the peer whose RFCOMM iAP2 DLC was brought UP,
# instead of disconnecting every paired Vita Bluetooth device.
bt = "btplugin/vitaplaybt.c"
one(bt,
    "static volatile int g_pipe_up;         /* iAP2 DLC is open + MSC done: byte pipe is usable */",
    "static volatile int g_pipe_up;         /* iAP2 DLC is open + MSC done: byte pipe is usable */\n"
    "/* Handoff peer: not all devices on the Vita's Bluetooth radio. */\n"
    "static volatile unsigned g_candidate_mac0, g_candidate_mac1;\n"
    "static volatile unsigned g_iap_mac0, g_iap_mac1;\n"
    "static volatile int g_iap_peer_ready;\n"
    "static volatile int g_handoff_issued;\n")

one(bt,
    "        g_conn = conn;\n"
    "        g_config_sent = 0;",
    "        g_conn = conn;\n"
    "        g_candidate_mac0 = mac0;\n"
    "        g_candidate_mac1 = mac1;\n"
    "        g_iap_peer_ready = 0;\n"
    "        g_handoff_issued = 0;\n"
    "        g_config_sent = 0;")

one(bt,
    "    g_pipe_up = 1;\n"
    "    rfcomm_grant_credits(128);",
    "    g_iap_mac0 = g_candidate_mac0;\n"
    "    g_iap_mac1 = g_candidate_mac1;\n"
    "    g_iap_peer_ready = 1;\n"
    "    g_pipe_up = 1;\n"
    "    log_line(\"v8 iAP2 peer tracked mac=%08x:%08x, ready for Wi-Fi handoff\", \n"
    "             g_iap_mac0, g_iap_mac1);\n"
    "    rfcomm_grant_credits(128);")

# Optional capability handshake prevents the v8 app from attempting to use the
# new syscall against an old installed plugin.
one(bt,
    "int vpbt_status(void) {\n",
    "#define VITAPLAY_BT_HANDOFF_API 0x56504238 /* 'VPB8' */\n"
    "int vpbt_handoff_version(void) { return VITAPLAY_BT_HANDOFF_API; }\n"
    "/* Invoked only upon AirPlay disableBluetooth command, never at startup. */\n"
    "int vpbt_handoff_disconnect(void) {\n"
    "    if (!g_iap_peer_ready || !g_pipe_up) {\n"
    "        log_line(\"v8 Wi-Fi handoff refused: no live iAP2 peer\");\n"
    "        return -2;\n"
    "    }\n"
    "    if (g_handoff_issued) return 1;\n"
    "    g_handoff_issued = 1;\n"
    "    int rc = ksceBtStartDisconnect(g_iap_mac0, g_iap_mac1);\n"
    "    log_line(\"v8 Wi-Fi handoff SceBtStartDisconnect mac=%08x:%08x result=0x%08x\",\n"
    "             g_iap_mac0, g_iap_mac1, (unsigned)rc);\n"
    "    if (rc < 0) g_handoff_issued = 0;\n"
    "    return rc;\n"
    "}\n"
    "\n"
    "int vpbt_status(void) {\n")

one(bt,
    '            if (ev.id == 0x05 && (ev.mac0 || ev.mac1)) {',
    '            if (ev.id == 0x06 && g_iap_peer_ready &&\n'
    '                ev.mac0 == g_iap_mac0 && ev.mac1 == g_iap_mac1) {\n'
    '                log_line("v8 Wi-Fi handoff confirmed: iAP2 Bluetooth peer disconnected");\n'
    '                g_pipe_up = 0;\n'
    '                g_iap_peer_ready = 0;\n'
    '                g_conn = NULL;\n'
    '                g_dcid = 0;\n'
    '            }\n'
    '            if (ev.id == 0x05 && (ev.mac0 || ev.mac1)) {')

exports = "btplugin/exports.yml"
one(exports, "    minor: 1", "    minor: 2")
one(exports, "        - vpbt_write\n", 
    "        - vpbt_write\n"
    "        - vpbt_handoff_version\n"
    "        - vpbt_handoff_disconnect\n")

# Userspace: the phone's command currently gets decoded and logged, then
# silently ignored. Make the handoff an explicit part of command processing.
sess = "src/session.c"
one(sess,
    'static void send_night_mode(session *s);\n',
    'static void send_night_mode(session *s);\n'
    'extern int vpbt_handoff_version(void);\n'
    'extern int vpbt_handoff_disconnect(void);\n'
    '#define VITAPLAY_BT_HANDOFF_API 0x56504238\n')

one(sess,
    '    if (dict && strcmp(pl_get_str(dict, "type"), "modesChanged") == 0) track_call_state(pl_get(dict, "params"));\n',
    '    if (dict && strcmp(pl_get_str(dict, "type"), "modesChanged") == 0)\n'
    '        track_call_state(pl_get(dict, "params"));\n'
    '    /* Only the already-established AirPlay control/event session can carry\n'
    '     * this command. Do not disconnect the Vita radio or unrelated devices.\n'
    '     * Apple documents this disconnect as part of Wi-Fi CarPlay handoff.\n'
    '     */\n'
    '    if (dict && strcmp(pl_get_str(dict, "type"), "disableBluetooth") == 0) {\n'
    '        int api = vpbt_handoff_version();\n'
    '        vp_log("v8 disableBluetooth received; plugin API=0x%08x", (unsigned)api);\n'
    '        if (api == VITAPLAY_BT_HANDOFF_API) {\n'
    '            int rc = vpbt_handoff_disconnect();\n'
    '            vp_log("v8 Wi-Fi handoff requested: disconnect iAP2 peer rc=0x%08x", (unsigned)rc);\n'
    '        } else {\n'
    '            vp_log("v8 HANDOFF NOT ATTEMPTED: incompatible/absent kernel plugin");\n'
    '        }\n'
    '    }\n')

main = "src/main.c"
one(main, "VitaPlay starting - v5 mDNS audio capability consistency test",
    "VitaPlay starting - v8 Wi-Fi audio Bluetooth handoff test")

# Ensure the patched source contains mandatory safety gates.
bt_text = (root / bt).read_text()
session_text = (root / sess).read_text()
assert "ksceBtStartDisconnect(g_iap_mac0, g_iap_mac1)" in bt_text
assert "g_iap_peer_ready" in bt_text
assert "if (api == VITAPLAY_BT_HANDOFF_API)" in session_text
assert "v8 Wi-Fi handoff requested" in session_text
print("v8 patches verified: disconnect ONLY tracked iAP2 peer after explicit iPhone command")
