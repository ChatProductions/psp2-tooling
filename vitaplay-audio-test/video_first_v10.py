#!/usr/bin/env python3
"""v10: video-first controlled test + RX-hook RTP-candidate diagnostics.

Run after hybrid_v9.py. First keep the Bluetooth SBC media capture OFF:
the first CarPlay video session must be established with the original iAP2
handling before A2DP capture/decoding is activated. No automatic BT disconnect.
"""
from pathlib import Path
root=Path("upstream/vitaplay")
def one(path, before, after):
    p=root/path
    s=p.read_text()
    n=s.count(before)
    if n != 1: raise SystemExit(f"v10 {path}: expected 1 anchor, found {n}: {before[:92]!r}")
    p.write_text(s.replace(before,after))
    print("v10 patch:", path, before[:75])

bt="btplugin/vitaplaybt.c"
one(bt,
    "static volatile unsigned g_hybrid_cid;\n",
    """static volatile unsigned g_hybrid_cid;
/* 10s diagnostics originate in the RX hook, *before* the SBC sync filter:
 * candidates=PT96 RTP packets, truncated=short/incomplete L2CAP,
 * rejected=unexpected non-SBC payload, prequeue_gaps=RTP sequence gaps
 * already visible before our kernel ring. These discriminate radio/SceBt
 * delivery problems from our app/queue decoding.
 */
static volatile int g_hybrid_enabled;
static volatile unsigned g_rtp_candidate,g_rtp_truncated,g_rtp_rejected;
static volatile unsigned g_rtp_prequeue_missing,g_rtp_duplicate;
static volatile unsigned g_rtp_prev_seq,g_rtp_have_seq;
int vpbt_audio_enable(int enabled) {
    g_hybrid_enabled=enabled ? 1 : 0;
    return g_hybrid_enabled;
}
""")

old="""    if (!p || n < 22) return;
    unsigned l2len = (unsigned)p[4] | ((unsigned)p[5] << 8);
    unsigned cid = (unsigned)p[6] | ((unsigned)p[7] << 8);
    if (cid < 0x40 || l2len < 14 || l2len > V9_A2DP_MAX ||
        (unsigned)(n - 8) < l2len) return;
    const unsigned char *r = p + 8;
    /* RTP v2/CC0, payload type 96, SBC frame count and SBC sync=9c. */
    if (r[0] != 0x80 || (r[1] & 0x7f) != 96 ||
        (r[12] & 0x0f) == 0 || r[13] != 0x9c) return;
    g_hybrid_cid = cid;
    __sync_add_and_fetch(&g_hybrid_seen, 1);"""
new="""    if (!g_hybrid_enabled || !p || n < 16) return;
    unsigned l2len = (unsigned)p[4] | ((unsigned)p[5] << 8);
    unsigned cid = (unsigned)p[6] | ((unsigned)p[7] << 8);
    if (cid < 0x40 || n < 22) return;
    const unsigned char *r = p + 8;
    /* Inspect every candidate RTP/PT96 packet first; the previous version
     * counted only those already passing the SBC sync filter.
     */
    if (r[0] != 0x80 || (r[1] & 0x7f) != 96) return;
    __sync_add_and_fetch(&g_rtp_candidate,1);
    if (l2len < 14 || l2len > V9_A2DP_MAX ||
        (unsigned)(n-8) < l2len) {
        __sync_add_and_fetch(&g_rtp_truncated,1);
        return;
    }
    if ((r[12] & 0x0f)==0 || r[13]!=0x9c) {
        __sync_add_and_fetch(&g_rtp_rejected,1);
        return;
    }
    if (g_hybrid_cid != cid) {
        g_hybrid_cid = cid;
        g_rtp_have_seq = 0;
    }
    unsigned seq=((unsigned)r[2]<<8)|r[3];
    if (g_rtp_have_seq) {
        unsigned delta=(seq - g_rtp_prev_seq)&0xffffu;
        if (delta==0) ++g_rtp_duplicate;
        else if (delta>1 && delta<0x8000u) g_rtp_prequeue_missing += delta-1;
    }
    g_rtp_prev_seq=seq;
    g_rtp_have_seq=1;
    __sync_add_and_fetch(&g_hybrid_seen, 1);"""
one(bt,old,new)

one(bt,
    '                log_line("v9 hybrid SBC RTP cid=0x%04x packets_5s=%u total=%u queued=%u dropped=%u",\n'
    '                         g_hybrid_cid,now-last_hybrid_seen,now,\n'
    '                         g_hybrid_wr-g_hybrid_rd,g_hybrid_dropped);',
    '                log_line("v10 BT RTP cid=0x%04x ok_5s=%u total=%u queued=%u queue_drops=%u cand=%u incomplete=%u filter_reject=%u prequeue_missing=%u duplicates=%u",\n'
    '                         g_hybrid_cid,now-last_hybrid_seen,now,\n'
    '                         g_hybrid_wr-g_hybrid_rd,g_hybrid_dropped,\n'
    '                         g_rtp_candidate,g_rtp_truncated,g_rtp_rejected,\n'
    '                         g_rtp_prequeue_missing,g_rtp_duplicate);')

one("btplugin/exports.yml",
    "        - vpbt_audio_cid\n",
    "        - vpbt_audio_cid\n"
    "        - vpbt_audio_enable\n")
one("btplugin/exports.yml","    minor: 3","    minor: 4")

# The kernel capture and SBC worker are enabled ONLY after video setup.
main="src/main.c"
one(main,
    "    bt_audio_start(); /* v9 hybrid SBC receiver, separate from video */\n",
    "    vp_log(\"v10 VIDEO-FIRST: Bluetooth SBC capture disabled until CarPlay video reaches STREAMING\");\n")
one(main,
    "    bool overlay = false;",
    "    bool hybrid_audio_started = false;\n"
    "    bool overlay = false;")
one(main,
    "        if (state == SESSION_STREAMING) {\n",
    '        if (state == SESSION_STREAMING) {\n'
    '            if (!hybrid_audio_started) {\n'
    '                hybrid_audio_started = true;\n'
    '                vp_log("v10 VIDEO STREAMING: enable in-app Bluetooth SBC receiver now");\n'
    '                bt_audio_start();\n'
    '            }\n')
one(main,
    "VitaPlay starting - v9 Bluetooth audio plus Wi-Fi CarPlay video",
    "VitaPlay starting - v10 VIDEO-FIRST CarPlay + Bluetooth SBC diagnostics")

# App activates the kernel RX capture immediately before starting the
# decoder. v9's worker already drains existing RTP packets before playback.
aud="src/btaudio.c"
one(aud,
    "extern int vpbt_audio_cid(void);\n",
    "extern int vpbt_audio_cid(void);\n"
    "extern int vpbt_audio_enable(int enable);\n")
one(aud,
    '    vp_log("v9 hybrid mode: retaining Bluetooth for SBC, CarPlay video on Wi-Fi");\n',
    '    vp_log("v10 hybrid mode: CarPlay video already streaming; enabling Bluetooth SBC capture");\n'
    '    int enabled=vpbt_audio_enable(1);\n'
    '    vp_log("v10 kernel Bluetooth SBC capture enabled=%d",enabled);\n'
    '    if (enabled != 1) return;\n')
# Keep prior v9 audio status markers; only add stats and safe activation.

source=(root/bt).read_text()
assert 'g_hybrid_enabled' in source and 'prequeue_missing' in source
assert 'vpbt_audio_enable' in (root/"btplugin/exports.yml").read_text()
assert 'v10 VIDEO STREAMING' in (root/main).read_text()
assert 'vpbt_audio_enable(1)' in (root/aud).read_text()
print("v10 verified: video first, gated BT capture, upstream loss diagnostic")
