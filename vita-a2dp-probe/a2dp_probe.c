/*
 * Vita A2DP Passive Probe v0.1 for firmware 3.65.
 * Uses the previously verified SceBt 3.65 RX hook (seg0 + 0x6558).
 *
 * PASSIVE: no outgoing Bluetooth requests, no configuration mutations,
 * no RFCOMM/iAP2 injection, and no audio payload capture.
 * The receive hook only copies short metadata to a ring buffer.
 *
 * Log: ux0:data/VitaPlay/a2dp_probe.log
 * The kernel plugin needs a reboot + an entry under *KERNEL; a VPK
 * alone cannot install a kernel RX hook.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>
#include <taihen.h>
#include <stdint.h>
#include <stdarg.h>

#define LOG_PATH "ux0:data/VitaPlay/a2dp_probe.log"
#define MODULE_NID_365 0x67e0c2eb
#define SIG_SEGMENT 0
#define SIG_OFFSET 0x6558
#define MAX_CID 256
#define OTHER_CID MAX_CID
#define RING_SIZE 128
#define PREVIEW_PER_CID 5

typedef struct {
    uint16_t cid;
    uint16_t payload_len;
    uint16_t arg0;
    uint16_t arg1;
    uint16_t arg2;
    uint8_t event_kind; /* 1 = signaling, 2 = initial CID sample */
    uint8_t sig_code;
    uint8_t sig_id;
    uint8_t first0;
    uint8_t first1;
} Record;

static Record g_ring[RING_SIZE];
static volatile unsigned g_written;
static volatile unsigned g_read;
static volatile unsigned g_dropped;
static volatile unsigned g_packets[MAX_CID + 1];
static volatile unsigned g_bytes[MAX_CID + 1];
static unsigned g_last_packets[MAX_CID + 1];
static tai_hook_ref_t g_ref;
static SceUID g_hook = -1;
static SceUID g_worker = -1;
static volatile int g_stop;

static unsigned u16le(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void log_line(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';
    SceUID fd = ksceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd < 0) return;
    ksceIoWrite(fd, line, n);
    ksceIoClose(fd);
}

/* This must never block or write to disk. */
static void enqueue(const Record *r) {
    unsigned w = g_written;
    if (w - g_read >= RING_SIZE) {
        ++g_dropped;
        return;
    }
    g_ring[w % RING_SIZE] = *r;
    __sync_synchronize();
    g_written = w + 1;
}

/*
 * SceBt's incoming ACL packet wrapper on this firmware:
 *   [HCI handle:2][ACL length:2][L2CAP len:2][CID:2][payload...]
 * For CID 1, payload is an L2CAP signaling command:
 *   [code:1][identifier:1][command len:2][arguments...]
 */
static int hook_rx(void *connection, const unsigned char *packet, int n) {
    if (packet && n >= 8) {
        unsigned cid = u16le(packet + 6);
        unsigned plen = u16le(packet + 4);
        unsigned slot = cid < MAX_CID ? cid : OTHER_CID;
        unsigned seen = __sync_add_and_fetch(&g_packets[slot], 1);
        __sync_fetch_and_add(&g_bytes[slot], plen);

        Record rec = {0};
        rec.cid = (uint16_t)cid;
        rec.payload_len = (uint16_t)plen;

        if (cid == 1 && n >= 12) {
            rec.event_kind = 1;
            rec.sig_code = packet[8];
            rec.sig_id = packet[9];
            if (n >= 14) rec.arg0 = (uint16_t)u16le(packet + 12);
            if (n >= 16) rec.arg1 = (uint16_t)u16le(packet + 14);
            if (n >= 18) rec.arg2 = (uint16_t)u16le(packet + 16);
            enqueue(&rec);
        } else if (cid >= 0x40 && seen <= PREVIEW_PER_CID && n >= 9) {
            rec.event_kind = 2;
            rec.first0 = packet[8];
            if (n >= 10) rec.first1 = packet[9];
            enqueue(&rec);
        }
    }
    return TAI_CONTINUE(int, g_ref, connection, packet, n);
}

static const char *sig_name(unsigned code) {
    switch (code) {
    case 1: return "COMMAND_REJECT";
    case 2: return "CONNECT_REQ";
    case 3: return "CONNECT_RSP";
    case 4: return "CONFIG_REQ";
    case 5: return "CONFIG_RSP";
    case 6: return "DISCONNECT_REQ";
    case 7: return "DISCONNECT_RSP";
    case 8: return "ECHO_REQ";
    case 9: return "ECHO_RSP";
    case 10: return "INFO_REQ";
    case 11: return "INFO_RSP";
    default: return "OTHER";
    }
}

static void drain(void) {
    while (g_read != g_written) {
        unsigned r = g_read;
        __sync_synchronize();
        Record rec = g_ring[r % RING_SIZE];
        g_read = r + 1;
        if (rec.event_kind == 1) {
            if (rec.sig_code == 2) {
                log_line("SIG %s id=%u PSM=0x%04x remote_scid=0x%04x%s",
                         sig_name(rec.sig_code), rec.sig_id, rec.arg0, rec.arg1,
                         rec.arg0 == 0x19 ? " [AVDTP]" :
                         rec.arg0 == 0x17 ? " [AVCTP]" : "");
            } else if (rec.sig_code == 3) {
                log_line("SIG %s id=%u local_cid=0x%04x remote_cid=0x%04x result=%u",
                         sig_name(rec.sig_code), rec.sig_id, rec.arg0, rec.arg1, rec.arg2);
            } else if (rec.sig_code == 4 || rec.sig_code == 5) {
                log_line("SIG %s id=%u cid=0x%04x opts_or_flags=0x%04x result=0x%04x",
                         sig_name(rec.sig_code), rec.sig_id, rec.arg0, rec.arg1, rec.arg2);
            } else if (rec.sig_code == 6 || rec.sig_code == 7) {
                log_line("SIG %s id=%u cid1=0x%04x cid2=0x%04x",
                         sig_name(rec.sig_code), rec.sig_id, rec.arg0, rec.arg1);
            }
        } else if (rec.event_kind == 2) {
            /* Header-only sample: first two bytes could be AVDTP/RTP, or another service.
             * We deliberately do not label all data CIDs as A2DP or log music payloads. */
            log_line("DATA cid=0x%04x l2cap_bytes=%u first2=%02x %02x",
                     rec.cid, rec.payload_len, rec.first0, rec.first1);
        }
    }
}

static void summarize(void) {
    for (unsigned cid = 0x40; cid <= MAX_CID; cid++) {
        unsigned packets = g_packets[cid];
        if (packets == g_last_packets[cid]) continue;
        unsigned delta = packets - g_last_packets[cid];
        g_last_packets[cid] = packets;
        if (cid < MAX_CID)
            log_line("RATE cid=0x%02x packets_5s=%u total=%u bytes_total=%u",
                     cid, delta, packets, g_bytes[cid]);
        else
            log_line("RATE other_cids packets_5s=%u total=%u bytes_total=%u",
                     delta, packets, g_bytes[cid]);
    }
    if (g_dropped) log_line("ring_overflow=%u", g_dropped);
}

static int worker_thread(SceSize argc, void *arg) {
    unsigned ticks = 0;
    while (!g_stop) {
        drain();
        ksceKernelDelayThread(100000);
        if (++ticks == 50) {
            summarize();
            ticks = 0;
        }
    }
    drain();
    summarize();
    log_line("Vita A2DP passive probe stopped");
    return 0;
}

int module_start(SceSize argc, const void *args) {
    ksceIoMkdir("ux0:data/VitaPlay", 0777);
    log_line("=== Vita A2DP passive probe 0.1, FW3.65, metadata-only ===");

    tai_module_info_t mod;
    mod.size = sizeof(mod);
    int rc = taiGetModuleInfoForKernel(KERNEL_PID, "SceBt", &mod);
    if (rc < 0) {
        log_line("SceBt unavailable rc=0x%08x, no hook installed", (unsigned)rc);
        return SCE_KERNEL_START_SUCCESS;
    }
    if (mod.module_nid != MODULE_NID_365) {
        log_line("UNSUPPORTED SceBt NID=0x%08x (expected 0x%08x): refusing hook",
                 (unsigned)mod.module_nid, (unsigned)MODULE_NID_365);
        return SCE_KERNEL_START_SUCCESS;
    }
    g_hook = taiHookFunctionOffsetForKernel(KERNEL_PID, &g_ref, mod.modid,
                                           SIG_SEGMENT, SIG_OFFSET, 1, hook_rx);
    if (g_hook < 0) {
        log_line("RX hook installation failed rc=0x%08x", (unsigned)g_hook);
        return SCE_KERNEL_START_SUCCESS;
    }
    log_line("RX hook installed on SceBt 3.65 (seg0+0x6558)");
    g_worker = ksceKernelCreateThread("a2dp_probe_log", worker_thread, 0x3c, 0x2000,
                                     0, 0x10000, 0);
    if (g_worker >= 0) ksceKernelStartThread(g_worker, 0, 0);
    else log_line("worker thread creation failed rc=0x%08x", (unsigned)g_worker);
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    if (g_hook >= 0) {
        taiHookReleaseForKernel(g_hook, g_ref);
        g_hook = -1;
    }
    g_stop = 1;
    if (g_worker >= 0) {
        ksceKernelWaitThreadEnd(g_worker, 0, 0);
        ksceKernelDeleteThread(g_worker);
        g_worker = -1;
    }
    return SCE_KERNEL_STOP_SUCCESS;
}
