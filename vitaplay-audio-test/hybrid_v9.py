#!/usr/bin/env python3
"""VitaPlay v9: hybrid CarPlay video over Wi-Fi + SBC audio over Bluetooth.

One SceBt 3.65 RX hook supplies existing iAP2 bootstrap AND a separate
bounded A2DP media queue. The kernel-side hook does only short memory copies.
No second kernel hook and no forced Bluetooth disconnect.
"""
from pathlib import Path
root=Path("upstream/vitaplay")
def one(path,old,new):
    f=root/path
    s=f.read_text()
    n=s.count(old)
    if n!=1: raise SystemExit(f"v9 {path}: anchor hit count {n}: {old[:100]!r}")
    f.write_text(s.replace(old,new))
    print(f"v9 modified {path}: {old[:45]!r}")

bt="btplugin/vitaplaybt.c"
one(bt, "static volatile unsigned g_dropped;\n", """static volatile unsigned g_dropped;

/* ---- v9: parallel Bluetooth A2DP SBC media queue ----
 * Data is received by the SAME existing SceBt hook as iAP2; no duplicate
 * taiHEN hook is installed. The hook does not intercept or alter the packet,
 * so the phone can keep video-over-Wi-Fi and the iAP2 BT session alive.
 *
 * iPhone RTP/SBC media observed in the standalone receiver:
 *   12-byte RTP header, 1-byte SBC packet count, SBC sync at byte 13.
 * Only the specific RTP header is copied; no audio payload is written to
 * disk. One producer (SceBt RX) and one reader (VitaPlay audio thread).
 */
#define V9_A2DP_RING 128u
#define V9_A2DP_MAX 1200u
typedef struct {
    unsigned short len;
    unsigned char bytes[V9_A2DP_MAX];
} HybridPacket;
static HybridPacket g_hybrid[V9_A2DP_RING];
static volatile unsigned g_hybrid_wr, g_hybrid_rd;
static volatile unsigned g_hybrid_seen, g_hybrid_dropped;
static volatile unsigned g_hybrid_cid;

static void hybrid_capture(const unsigned char *p, int n) {
    if (!p || n < 22) return;
    unsigned l2len = (unsigned)p[4] | ((unsigned)p[5] << 8);
    unsigned cid = (unsigned)p[6] | ((unsigned)p[7] << 8);
    if (cid < 0x40 || l2len < 14 || l2len > V9_A2DP_MAX ||
        (unsigned)(n - 8) < l2len) return;
    const unsigned char *r = p + 8;
    /* RTP v2/CC0, payload type 96, SBC frame count and SBC sync=9c. */
    if (r[0] != 0x80 || (r[1] & 0x7f) != 96 ||
        (r[12] & 0x0f) == 0 || r[13] != 0x9c) return;
    g_hybrid_cid = cid;
    __sync_add_and_fetch(&g_hybrid_seen, 1);
    unsigned wr=g_hybrid_wr;
    if (wr-g_hybrid_rd >= V9_A2DP_RING) {
        __sync_add_and_fetch(&g_hybrid_dropped, 1);
        return;
    }
    HybridPacket *dst=&g_hybrid[wr % V9_A2DP_RING];
    dst->len=(unsigned short)l2len;
    for (unsigned i=0;i<l2len;i++) dst->bytes[i]=r[i];
    __sync_synchronize();
    g_hybrid_wr=wr+1;
}

/* Usermode syscalls in the EXISTING VitaPlayBt library. */
int vpbt_audio_read(void *user_buf, int max_bytes) {
    unsigned rd=g_hybrid_rd;
    if (rd==g_hybrid_wr) return 0;
    HybridPacket *pkt=&g_hybrid[rd % V9_A2DP_RING];
    if (!user_buf || max_bytes < pkt->len) return -2;
    if (ksceKernelCopyToUser(user_buf,pkt->bytes,pkt->len)<0) return -1;
    __sync_synchronize();
    g_hybrid_rd=rd+1;
    return pkt->len;
}
int vpbt_audio_packets(void) { return (int)g_hybrid_seen; }
int vpbt_audio_dropped(void) { return (int)g_hybrid_dropped; }
int vpbt_audio_cid(void) { return (int)g_hybrid_cid; }
""")

one(bt,
    "static int hook_l2cap_sig(void *conn, const unsigned char *pdu, int len) {\n",
    "static int hook_l2cap_sig(void *conn, const unsigned char *pdu, int len) {\n"
    "    hybrid_capture(pdu,len); /* BEFORE the old iAP2 CID filtering */\n")

# A2DP packet traffic bypasses the iAP2 logging queue; log aggregate metrics
# only, every 5 seconds, without writing anything from the RX hook.
one(bt,
    "static int worker(SceSize args, void *argp) {\n    unsigned last_dropped = 0;\n",
    "static int worker(SceSize args, void *argp) {\n"
    "    unsigned last_dropped = 0;\n"
    "    unsigned last_hybrid_seen=0;\n"
    "    unsigned ticks=0;\n")
one(bt,
    '        ksceKernelDelayThread(100 * 1000);\n    }\n    drain();',
    '        if (++ticks >= 50) { /* 5 second summary, avoiding log churn */\n'
    '            ticks=0;\n'
    '            unsigned now=g_hybrid_seen;\n'
    '            if (now != last_hybrid_seen)\n'
    '                log_line("v9 hybrid SBC RTP cid=0x%04x packets_5s=%u total=%u queued=%u dropped=%u",\n'
    '                         g_hybrid_cid,now-last_hybrid_seen,now,\n'
    '                         g_hybrid_wr-g_hybrid_rd,g_hybrid_dropped);\n'
    '            last_hybrid_seen=now;\n'
    '        }\n'
    '        ksceKernelDelayThread(100 * 1000);\n'
    '    }\n'
    '    drain();')

exp="btplugin/exports.yml"
one(exp, "        - vpbt_write\n",
    "        - vpbt_write\n"
    "        - vpbt_audio_read\n"
    "        - vpbt_audio_packets\n"
    "        - vpbt_audio_dropped\n"
    "        - vpbt_audio_cid\n")
one(exp,"    minor: 1","    minor: 3")

# Build the decoder as Vita native code, not a separate receiver VPK.
src=Path("vitaplay-audio-test/hybrid_btaudio.c").read_text()
header=Path("vitaplay-audio-test/hybrid_btaudio.h").read_text()
(root/"src/btaudio.c").write_text(src)
(root/"src/btaudio.h").write_text(header)

m="Makefile"
one(m,"SRC  := $(wildcard src/*.c)","SRC  := $(wildcard src/*.c)\nSBC_SRC := vendor/libsbc/src/sbc.c vendor/libsbc/src/bits.c\nSBC_OBJ := build/sbc.o build/bits.o")
one(m,"-Wno-format-truncation","-Wno-format-truncation -Ivendor/libsbc/include")
one(m,"build/$(TARGET).elf: $(OBJ) $(BTPLUGIN_STUB)\n\t$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@",
      "build/sbc.o: vendor/libsbc/src/sbc.c | build\n\t$(CC) $(CFLAGS) -c $< -o $@\n\n"
      "build/bits.o: vendor/libsbc/src/bits.c | build\n\t$(CC) $(CFLAGS) -c $< -o $@\n\n"
      "build/$(TARGET).elf: $(OBJ) $(SBC_OBJ) $(BTPLUGIN_STUB)\n"
      "\t$(CC) $(LDFLAGS) $(OBJ) $(SBC_OBJ) $(LIBS) -o $@")
one(m,"VERSION  := 00.10","VERSION  := 00.90")

main="src/main.c"
one(main,'#include "btboot.h"','#include "btboot.h"\n#include "btaudio.h"')
one(main,"    btboot_start();\n","    btboot_start();\n"
    "    bt_audio_start(); /* v9 hybrid SBC receiver, separate from video */\n")
one(main,"VitaPlay starting - v5 mDNS audio capability consistency test",
    "VitaPlay starting - v9 Bluetooth audio plus Wi-Fi CarPlay video")

# Make it explicit in the app log when iPhone requests BT disconnection. v7
# ignored it; v9 deliberately preserves the link for hybrid audio.
sess="src/session.c"
one(sess,
    '    if (dict && strcmp(pl_get_str(dict, "type"), "modesChanged") == 0) track_call_state(pl_get(dict, "params"));',
    '    if (dict && strcmp(pl_get_str(dict, "type"), "modesChanged") == 0) track_call_state(pl_get(dict, "params"));\n'
    '    if (dict && strcmp(pl_get_str(dict, "type"), "disableBluetooth") == 0)\n'
    '        vp_log("v9 hybrid mode: retain Bluetooth link for SBC while CarPlay video uses Wi-Fi");')

# Old /info / mDNS and video code is not changed. We want to test BT routing
# alongside the existing v7 video first.
assert "hybrid_capture(pdu,len)" in (root/bt).read_text()
assert "vpbt_audio_read" in (root/exp).read_text()
assert "bt_audio_start()" in (root/main).read_text()
print("v9 hybrid: one L2CAP hook, iAP2 + A2DP media, BT audio + Wi-Fi video")
