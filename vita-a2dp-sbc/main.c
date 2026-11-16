/*
 * Vita A2DP SBC Receiver v0.4 (standalone prototype).
 * Requires matching experimental kernel plugin installed through taiHEN.
 * Uses Google libsbc (Apache-2.0) to decode inbound iPhone SBC/RTP frames.
 * No Bluetooth pairing, control, injection, or transmitter code in this VPK.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#include <sbc.h>

#define APP_LOG "ux0:data/VitaPlay/sbc_playback.log"
#define AUDIO_RATE 44100
#define GRAIN 1024
#define MAX_PACKET 1200

int _newlib_heap_size_user = 24 * 1024 * 1024;

/* Generated VitaSDK syscall stubs from kernel companion plugin. */
extern int a2dp_audio_read(void *dst, int max);
extern int a2dp_audio_packets(void);
extern int a2dp_audio_dropped(void);
extern int a2dp_audio_cid(void);

static volatile int g_running = 1;
static volatile unsigned g_rcvd, g_pcm_output, g_bad, g_sbc_fail, g_bad_shape;
static volatile int g_port = -1;
static volatile int g_reader_error;
static pthread_t g_thread;
static FILE *g_log;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void report(const char *fmt, ...) {
    char s[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(s, sizeof(s), fmt, args);
    va_end(args);
    pthread_mutex_lock(&g_log_lock);
    if (g_log) {
        fprintf(g_log, "%s\n", s);
        fflush(g_log);
    }
    pthread_mutex_unlock(&g_log_lock);
}

static void *audio_worker(void *arg) {
    (void)arg;
    uint8_t packet[MAX_PACKET];
    int16_t stereo[GRAIN * 2];
    sbc_t decoder;
    sbc_reset(&decoder);

    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, GRAIN,
                                   AUDIO_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    g_port = port;
    report("Audio port open rc=0x%08x, 44.1kHz stereo, samples=%d", (unsigned)port, GRAIN);
    if (port < 0) return NULL;

    while (g_running) {
        int n = a2dp_audio_read(packet, sizeof(packet));
        if (n == 0) {
            sceKernelDelayThread(5000);
            continue;
        }
        if (n < 0) {
            g_reader_error = n;
            if (g_rcvd == 0 && g_bad == 0) report("No valid kernel packet reader: rc=%d", n);
            sceKernelDelayThread(250000);
            continue;
        }
        ++g_rcvd;
        /* 12-byte fixed RTP header; A2DP SBC 1-byte header; first SBC frame. */
        if (n < 17 || packet[0] != 0x80 || (packet[1] & 0x7f) != 96 ||
            packet[13] != 0x9c) {
            ++g_bad;
            continue;
        }
        int frames = packet[12] & 0x0f;
        if (frames != 8) { /* this hardware capture consistently uses 8 x 128 samples */
            ++g_bad_shape;
            if (g_bad_shape <= 4)
                report("Unexpected SBC/RTP frame count=%d n=%d", frames, n);
            continue;
        }

        struct sbc_frame frame;
        int prc = sbc_probe(packet + 13, &frame);
        unsigned flen = prc == 0 ? sbc_get_frame_size(&frame) : 0;
        if (!flen || flen > 150 || frame.freq != SBC_FREQ_44K1 ||
            frame.nsubbands != 8 || frame.nblocks != 16 ||
            frame.mode != SBC_MODE_JOINT_STEREO ||
            13 + (unsigned)frames * flen > (unsigned)n) {
            ++g_bad_shape;
            if (g_bad_shape <= 4)
                report("Invalid SBC pkt probe=%d len=%u payload=%d freq=%dHz mode=%d blocks=%d subbands=%d", prc, flen, n,
                       prc == 0 ? sbc_get_freq_hz(frame.freq) : -1,
                       prc == 0 ? (int)frame.mode : -1,
                       prc == 0 ? frame.nblocks : -1,
                       prc == 0 ? frame.nsubbands : -1);
            continue;
        }

        int dec_ok = 1;
        for (int i = 0; i < frames; ++i) {
            int16_t *out = stereo + i * 128 * 2;
            struct sbc_frame actual;
            int rc = sbc_decode(&decoder, packet + 13 + i * flen, flen,
                                &actual, out, 2, out + 1, 2);
            if (rc != 0) {
                ++g_sbc_fail;
                if (g_sbc_fail <= 4)
                    report("SBC decode failed rc=%d frame=%d pkt=%u", rc, i, g_rcvd);
                dec_ok = 0;
                break;
            }
        }
        if (!dec_ok) continue;

        int result = sceAudioOutOutput(port, stereo);
        if (result < 0) {
            if (++g_bad <= 4) report("AudioOutOutput returned rc=0x%08x", (unsigned)result);
            sceKernelDelayThread(10000);
        } else {
            g_pcm_output += GRAIN;
        }
        if (g_rcvd % 128 == 0) {
            report("packets=%u decoded_samples=%u failures=%u badshape=%u dropped=%d cid=0x%x",
                   g_rcvd, g_pcm_output, g_sbc_fail, g_bad_shape,
                   a2dp_audio_dropped(), a2dp_audio_cid());
        }
    }
    sceAudioOutReleasePort(port);
    g_port = -1;
    report("Audio thread exiting packets=%u decoded_samples=%u", g_rcvd, g_pcm_output);
    return NULL;
}

static void draw(vita2d_pgf *font, int x, int y, unsigned color, const char *fmt, ...) {
    char s[220];
    va_list args;
    va_start(args, fmt);
    vsnprintf(s, sizeof(s), fmt, args);
    va_end(args);
    vita2d_pgf_draw_text(font, x, y, color, 1.0f, s);
}

int main(void) {
    sceIoMkdir("ux0:data/VitaPlay", 0777);
    g_log = fopen(APP_LOG, "w");
    report("=== Vita A2DP SBC Receiver v0.4, 44100 Hz stereo ===");
    report("Codec: SBC joint stereo, 16 blocks, 8 subbands; audio RTP type 96");

    vita2d_init();
    vita2d_set_clear_color(RGBA8(15,17,27,255));
    vita2d_pgf *font = vita2d_load_default_pgf();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    int thread_rc = pthread_create(&g_thread, NULL, audio_worker, NULL);
    report("Audio thread start rc=%d", thread_rc);
    if (thread_rc != 0) g_running = 0;

    while (1) {
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START) break;

        vita2d_start_drawing();
        vita2d_clear_screen();
        const unsigned white = RGBA8(236,240,245,255);
        const unsigned gray  = RGBA8(155,166,177,255);
        const unsigned green = RGBA8(108,219,152,255);
        draw(font, 38, 58, white, "Vita A2DP SBC Receiver v0.4");
        draw(font, 38, 100, gray, "Standalone iPhone Bluetooth audio test - VitaPlay closed");
        draw(font, 38, 140, white, "Codec: SBC 44.1 kHz joint stereo, 8 frames x 128 samples");
        draw(font, 38, 180, g_port >= 0 ? green : gray, "sceAudioOut port: %d", g_port);
        draw(font, 38, 220, white, "Incoming RTP packets: %u (kernel total %d)",
             g_rcvd, a2dp_audio_packets());
        draw(font, 38, 260, white, "Decoded PCM samples: %u", g_pcm_output);
        draw(font, 38, 300, white, "Kernel drops: %d    stream CID: 0x%x",
             a2dp_audio_dropped(), a2dp_audio_cid());
        draw(font, 38, 340, gray, "Decode failures: %u    unsupported packets: %u", g_sbc_fail, g_bad_shape);
        draw(font, 38, 380, gray, "Reader status: %d", g_reader_error);
        draw(font, 38, 434, white, "On iPhone, select PlayStation Vita and play music.");
        draw(font, 38, 464, gray, "START = exit | Log: ux0:data/VitaPlay/sbc_playback.log");
        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceKernelDelayThread(33000);
    }
    g_running = 0;
    if (thread_rc == 0) pthread_join(g_thread, NULL);
    vita2d_free_pgf(font);
    vita2d_fini();
    if (g_log) fclose(g_log);
    sceKernelExitProcess(0);
    return 0;
}
