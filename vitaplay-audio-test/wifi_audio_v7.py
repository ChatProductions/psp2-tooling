#!/usr/bin/env python3
"""VitaPlay v7: conservative AirPlay audio capability and event-channel repair.

Run after aac_patch.py and disconnect_diag.py.
Uses source-only patches; never modifies MFi identity, credentials, video codec,
screen stream handlers, or Bluetooth kernel plugin.
"""
from pathlib import Path

root = Path("upstream/vitaplay")

def one(path, before, after):
    p = root / path
    s = p.read_text()
    n = s.count(before)
    if n != 1:
        raise SystemExit(f"v7 patch failed {path}: expected one anchor, found {n}: {before[:90]!r}")
    p.write_text(s.replace(before, after))
    print(f"v7 patched {path}: {before[:60]}")

# v5's 48kHz-only mask removed the 44.1kHz bit positions despite the
# built-in PCM resampler and native AAC backend supporting both frequencies.
# Apple's sender is allowed to choose either bit 10/11 (44.1kHz) or 14/15 (48kHz).
one("src/info.c", "#define AUDIO_PCM 0xc3fc", "#define AUDIO_PCM 0xcffc")
one("src/info.c", "#define AUDIO_PCM_MONO 0x4154", "#define AUDIO_PCM_MONO 0x4554")
one("src/info.c", "#define AUDIO_AAC_LC 0x800000", "#define AUDIO_AAC_LC 0xc00000")
# Type 102 media was offered in formats, but absent from the latencies list.
# Keep the preexisting 'default' latency for cross-version compatibility.
one("src/info.c", '    pl_push(a, latency(102, "default"));',
    '    pl_push(a, latency(102, "default"));\n'
    '    pl_push(a, latency(102, "media"));')

# The current firmware answers every phone-initiated RTSP event with 200 but
# silently DROPS the actual POST /command body. It thereby ignores
# modesChanged, setUpStreams, duckAudio, flushAudio, etc. on that channel.
# Decode/log those commands without issuing unsanctioned stream opens.
one("src/session.c",
    "static void send_night_mode(session *s);",
    "static void send_night_mode(session *s);\n"
    "static void handle_command(const rtsp_msg *req); /* event-channel dispatch */")

one("src/session.c",
    '            /* Phone-initiated requests get an empty 200; responses to our commands are ignored. */\n'
    '            if (strncmp(msg.method, "RTSP/", 5) != 0 && strncmp(msg.method, "HTTP/", 5) != 0) {',
    '            /* v7: do not silently discard phone-initiated event commands.\n'
    '             * Their response remains an empty 200, as in the known-working video path.\n'
    '             * Stream lifecycle commands are logged for diagnosis; they are NOT\n'
    '             * invented into SETUP until the real request format is confirmed.\n'
    '             */\n'
    '            if (strncmp(msg.method, "RTSP/", 5) != 0 && strncmp(msg.method, "HTTP/", 5) != 0) {\n'
    '                if (strcmp(msg.method, "POST") == 0 && ends_with_ci(msg.path, "/command")) {\n'
    '                    vp_log("v7 event channel POST /command (%u bytes)", (unsigned)msg.body_len);\n'
    '                    handle_command(&msg);\n'
    '                } else if (msg.body_len != 0) {\n'
    '                    vp_log("v7 event request: %s %s body=%u", msg.method, msg.path,\n'
    '                           (unsigned)msg.body_len);\n'
    '                }')

# On the initial AirPlay SETUP, negotiate only strings the phone actually
# proposes and that the receiver implements. Prior code returned two
# enabledFeatures unconditionally. This is aligned with real receiver behavior.
# Do NOT echo unsupported tokens, which can enable features the app lacks.
one("src/session.c",
    '        pl_node *features = pl_array();\n'
    '        pl_push(features, pl_str("iAPChannel"));\n'
    '        pl_push(features, pl_str("viewAreas"));\n'
    '        pl_set(response, "enabledFeatures", features);',
    '        pl_node *features = pl_array();\n'
    '        const pl_node *proposal = pl_get(dict, "features");\n'
    '        char feature_desc[300];\n'
    '        if (proposal) pl_describe(proposal, feature_desc, sizeof(feature_desc));\n'
    '        else snprintf(feature_desc, sizeof(feature_desc), "(none)");\n'
    '        vp_log("v7 initial SETUP proposed features: %s", feature_desc);\n'
    '        if (proposal && proposal->type == PL_ARRAY) {\n'
    '            for (size_t i = 0; i < proposal->count; ++i) {\n'
    '                const pl_node *item = proposal->items[i];\n'
    '                if (!item || item->type != PL_STRING) continue;\n'
    '                const char *name = item->str;\n'
    '                if (name && (!strcmp(name, "iAPChannel") || !strcmp(name, "viewAreas")))\n'
    '                    pl_push(features, pl_str(name));\n'
    '            }\n'
    '        }\n'
    '        char enabled_desc[180];\n'
    '        pl_describe(features, enabled_desc, sizeof(enabled_desc));\n'
    '        vp_log("v7 initial SETUP accepted features: %s", enabled_desc);\n'
    '        pl_set(response, "enabledFeatures", features);')

# Important diagnostic: control-side command and event-side command both use
# this function; don't log raw audio data, keys, or MFi material.
one("src/session.c",
    '    char desc[400];\n'
    '    if (dict) pl_describe(dict, desc, sizeof(desc));\n'
    '    vp_log("phone command: %s", dict ? desc : "?");',
    '    char desc[1200];\n'
    '    if (dict) pl_describe(dict, desc, sizeof(desc));\n'
    '    vp_log("v7 phone command: %s", dict ? desc : "?");\n'
    '    if (dict) {\n'
    '        const char *type = pl_get_str(dict, "type");\n'
    '        if (!strcmp(type, "setUpStreams") || !strcmp(type, "tearDownStreams")) {\n'
    '            const pl_node *params = pl_get(dict, "params");\n'
    '            const pl_node *streams = pl_get(params, "streams");\n'
    '            vp_log("v7 stream lifecycle command=%s streams=%d", type,\n'
    '                   streams && streams->type == PL_ARRAY ? (int)streams->count : -1);\n'
    '        }\n'
    '    }')

# Distinguish UI-only RECORD from audio stream SETUP without flooding the
# existing /info, session, and video diagnostics.
one("src/session.c",
    '        vp_log("CarPlay session active");',
    '        vp_log("CarPlay session active - v7 Wi-Fi audio negotiation diagnostic");')

# Source test: compile will also check the exact markers in the VPK's eboot.
info = (root / "src/info.c").read_text()
sess = (root / "src/session.c").read_text()
assert "AUDIO_PCM 0xcffc" in info and "AUDIO_AAC_LC 0xc00000" in info
assert 'latency(102, "media")' in info
assert 'v7 event channel POST /command' in sess
assert 'v7 stream lifecycle command' in sess
assert 'v7 initial SETUP accepted features' in sess
print("v7 PASS: 44.1/48kHz format offers, matching 102 latency, "
      "feature negotiation, event-command dispatch and diagnostics")
