/*
 * Vita A2DP SBC Receiver v0.6 (standalone prototype).
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

/* Decoded PCM FIFO (single producer, single consumer), 1.49 seconds at 44.1kHz.
 * Two threads isolate Bluetooth arrivals and SBC decoding from hardware playback.
 */
#define PCM_RING_FRAMES 65536u
#define PCM_RING_MASK (PCM_RING_FRAMES - 1u)
#define PREBUFFER_FRAMES (16u * GRAIN) /* ~372ms */
#define FADE_FRAMES 128
static int16_t g_pcm_ring[PCM_RING_FRAMES * 2];
static volatile unsigned g_pcm_head;
static volatile unsigned g_pcm_tail;
static volatile unsigned g_underruns, g_overflow, g_seq_gaps, g_rebuffer;
static volatile unsigned g_output_grains, g_silent_grains;
static volatile unsigned g_buffered_frames;
static volatile int g_tempo_milli = 1000;
static volatile int g_receiver_ready, g_playback_ready;
static pthread_t g_receiver_thread;
static pthread_t g_playback_thread;

static void *receiver_worker(void *arg) {
    (void)arg;
    uint8_t packet[MAX_PACKET];
    int16_t decoded[GRAIN * 2];
    sbc_t decoder;
    sbc_reset(&decoder);
    uint16_t previous_seq = 0;
    int have_seq = 0;
    g_receiver_ready = 1;
    report("RX/decoder thread started; playback is independent");

    while (g_running) {
        int n = a2dp_audio_read(packet, sizeof(packet));
        if (n == 0) {
            sceKernelDelayThread(2500);
            continue;
        }
        if (n < 0) {
            g_reader_error = n;
            if (g_rcvd == 0) report("Kernel packet read error=%d", n);
            sceKernelDelayThread(100000);
            continue;
        }
        ++g_rcvd;
        /* A2DP SBC media: fixed 12-byte RTP header, one SBC header and frames. */
        if (n < 17 || packet[0] != 0x80 || (packet[1] & 0x7f) != 96 ||
            packet[13] != 0x9c) {
            ++g_bad;
            continue;
        }
        uint16_t seq = (uint16_t)(((unsigned)packet[2] << 8) | packet[3]);
        if (have_seq) {
            unsigned missing = (unsigned)(uint16_t)(seq - previous_seq);
            if (missing > 1 && missing < 0x8000u) {
                g_seq_gaps += missing - 1;
                if (g_seq_gaps <= 8) report("RTP seq gap prev=%u now=%u missing=%u",
                                            previous_seq, seq, missing - 1);
            }
        }
        have_seq = 1;
        previous_seq = seq;

        int frames = packet[12] & 0x0f;
        if (frames != 8) {
            ++g_bad_shape;
            if (g_bad_shape <= 3) report("Unexpected SBC count=%d n=%d", frames, n);
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
            if (g_bad_shape <= 3)
                report("Unexpected SBC format probe=%d len=%u freq=%d framecount=%d",
                       prc, flen, prc == 0 ? sbc_get_freq_hz(frame.freq) : -1, frames);
            continue;
        }
        int ok = 1;
        for (int i = 0; i < 8; ++i) {
            int16_t *out = decoded + i * 128 * 2;
            struct sbc_frame actual;
            int rc = sbc_decode(&decoder, packet + 13 + i * flen, flen,
                                &actual, out, 2, out + 1, 2);
            if (rc != 0) {
                ++g_sbc_fail;
                if (g_sbc_fail <= 4) report("SBC decode failed=%d packet=%u frame=%d",
                                            rc, g_rcvd, i);
                ok = 0;
                break;
            }
        }
        if (!ok) continue;

        unsigned head = g_pcm_head, tail = g_pcm_tail;
        if (head - tail > PCM_RING_FRAMES - GRAIN) {
            ++g_overflow;
            continue;
        }
        for (unsigned i = 0; i < GRAIN; ++i) {
            unsigned dst = ((head + i) & PCM_RING_MASK) * 2u;
            g_pcm_ring[dst] = decoded[2*i];
            g_pcm_ring[dst+1] = decoded[2*i+1];
        }
        __sync_synchronize();
        g_pcm_head = head + GRAIN;
    }
    report("RX thread stopped packets=%u errors=%u missing_rtp=%u pcm_overflow=%u",
           g_rcvd, g_sbc_fail, g_seq_gaps, g_overflow);
    g_receiver_ready = 0;
    return NULL;
}

static int16_t clamp_pcm(float v) {
    if (v > 32767.f) return 32767;
    if (v < -32768.f) return -32768;
    return (int16_t)v;
}

static void *playback_worker(void *arg) {
    (void)arg;
    int16_t output[GRAIN * 2];
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN,
                                   AUDIO_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    g_port = port;
    report("v0.6 BGM audio port rc=0x%08x rate=%d Hz grain=%d",
           (unsigned)port, AUDIO_RATE, GRAIN);
    if (port < 0) return NULL;
    g_playback_ready = 1;

    /* The former receiver played each Bluetooth packet immediately. A short
     * initial buffer keeps normal radio scheduling jitter from creating gaps.
     * Only the output thread calls sceAudioOutOutput.
     */
    while (g_running && g_pcm_head - g_pcm_tail < PREBUFFER_FRAMES)
        sceKernelDelayThread(4000);
    if (!g_running) goto finish;
    report("Prebuffer full: %u PCM frames (~%u ms)", g_pcm_head - g_pcm_tail,
           (g_pcm_head - g_pcm_tail) * 1000u / AUDIO_RATE);

    float phase = 0.f;
    float rate = 1.f;
    int16_t last_l = 0, last_r = 0;
    int fade_in = 1;
    unsigned last_log_grain = 0;
    SceUInt64 last_time = sceKernelGetSystemTimeWide();

    while (g_running) {
        unsigned head = g_pcm_head;
        __sync_synchronize();
        unsigned tail = g_pcm_tail;
        unsigned available = head - tail;
        g_buffered_frames = available;

        /* Steer the playback clock gently using FIFO occupancy. This is a
         * small pitch/speed correction (up to 6%), not pitch-preserving WSOLA.
         * It complements the prebuffer when the Bluetooth clock drifts.
         */
        float target = available < 5u*GRAIN ? 0.94f :
                       available < 10u*GRAIN ? 0.97f :
                       available < 12u*GRAIN ? 0.99f :
                       available > 23u*GRAIN ? 1.018f :
                       available > 19u*GRAIN ? 1.006f : 1.0f;
        rate = rate * 0.96f + target * 0.04f;
        g_tempo_milli = (int)(rate * 1000.f);

        if (available < 2u*GRAIN) {
            /* Feed the hardware continuously during a radio gap. Previously
             * the app stopped sending output altogether, creating stutter.
             */
            ++g_underruns;
            ++g_silent_grains;
            for (int i = 0; i < GRAIN; ++i) {
                float gain = i < FADE_FRAMES ?
                    (float)(FADE_FRAMES - 1 - i) / (float)FADE_FRAMES : 0.f;
                output[2*i] = clamp_pcm((float)last_l * gain);
                output[2*i+1] = clamp_pcm((float)last_r * gain);
            }
            last_l = last_r = 0;
            fade_in = 1;
            /* Do not force-drain the remaining samples: wait for refill. */
        } else {
            unsigned used = 0;
            for (unsigned i = 0; i < GRAIN; ++i) {
                unsigned offset = (unsigned)phase;
                if (offset + 1 >= available) { /* safe against sparse arrivals */
                    output[2*i] = output[i ? 2*(i-1) : 0];
                    output[2*i+1] = output[i ? 2*(i-1)+1 : 1];
                    continue;
                }
                float fract = phase - (float)offset;
                unsigned a = ((tail + offset) & PCM_RING_MASK) * 2u;
                unsigned b = ((tail + offset + 1) & PCM_RING_MASK) * 2u;
                int16_t l = clamp_pcm((float)g_pcm_ring[a] * (1.f-fract) +
                                      (float)g_pcm_ring[b] * fract);
                int16_t r = clamp_pcm((float)g_pcm_ring[a+1] * (1.f-fract) +
                                      (float)g_pcm_ring[b+1] * fract);
                if (fade_in && i < FADE_FRAMES) {
                    float gain = (float)(i+1) / (float)FADE_FRAMES;
                    l = clamp_pcm((float)l * gain);
                    r = clamp_pcm((float)r * gain);
                }
                output[2*i] = l;
                output[2*i+1] = r;
                phase += rate;
            }
            fade_in = 0;
            used = (unsigned)phase;
            phase -= (float)used;
            __sync_synchronize();
            g_pcm_tail = tail + used;
            last_l = output[(GRAIN-1)*2];
            last_r = output[(GRAIN-1)*2+1];
        }
        int rc = sceAudioOutOutput(port, output); /* hardware paces us */
        if (rc < 0 && g_bad < 5) {
            ++g_bad;
            report("sceAudioOutOutput failed rc=0x%08x", (unsigned)rc);
        }
        ++g_output_grains;
        if (rc >= 0) g_pcm_output += GRAIN;

        /* Log metrics every ~3 seconds of output, with elapsed real-time rate. */
        if (g_output_grains - last_log_grain >= 128u) {
            SceUInt64 now = sceKernelGetSystemTimeWide();
            unsigned duration_ms = (unsigned)((now - last_time) / 1000);
            report("rx_packets=%u out_frames=%u fifo=%u (~%u ms) tempo=%d/1000 "
                   "underruns=%u silence_grains=%u rtp_gaps=%u kernel_drops=%d "
                   "pcm_overflow=%u bad_sbc=%u elapsed_ms=%u",
                   g_rcvd, g_pcm_output, g_pcm_head - g_pcm_tail,
                   (g_pcm_head - g_pcm_tail)*1000u/AUDIO_RATE, g_tempo_milli,
                   g_underruns, g_silent_grains, g_seq_gaps, a2dp_audio_dropped(),
                   g_overflow, g_sbc_fail, duration_ms);
            last_log_grain = g_output_grains;
            last_time = now;
        }
    }
finish:
    sceAudioOutReleasePort(port);
    g_port = -1;
    report("v0.6 playback stopped grains=%u samples=%u underruns=%u rx=%u",
           g_output_grains, g_pcm_output, g_underruns, g_rcvd);
    g_playback_ready = 0;
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
    report("=== Vita A2DP SBC Receiver v0.6, 44100 Hz stereo ===");
    report("Codec: SBC joint stereo, 16 blocks, 8 subbands; audio RTP type 96");

    vita2d_init();
    vita2d_set_clear_color(RGBA8(15,17,27,255));
    vita2d_pgf *font = vita2d_load_default_pgf();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    int rx_rc = pthread_create(&g_receiver_thread, NULL, receiver_worker, NULL);
    int tx_rc = rx_rc == 0 ? pthread_create(&g_playback_thread, NULL, playback_worker, NULL) : -1;
    report("Worker threads rx_rc=%d playback_rc=%d", rx_rc, tx_rc);
    if (rx_rc != 0 || tx_rc != 0) g_running = 0;

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
        draw(font, 38, 58, white, "Vita A2DP SBC Receiver v0.6");
        draw(font, 38, 100, gray, "Standalone iPhone Bluetooth audio test - VitaPlay closed");
        draw(font, 38, 140, white, "Codec: SBC 44.1 kHz joint stereo, 8 frames x 128 samples");
        draw(font, 38, 180, g_port >= 0 ? green : gray, "sceAudioOut port: %d", g_port);
        draw(font, 38, 220, white, "Incoming RTP packets: %u (kernel total %d)",
             g_rcvd, a2dp_audio_packets());
        draw(font, 38, 260, white, "Output PCM samples: %u   buffer: %u ms",
             g_pcm_output, g_buffered_frames*1000u/AUDIO_RATE);
        draw(font, 38, 300, white, "Kernel drops: %d    stream CID: 0x%x",
             a2dp_audio_dropped(), a2dp_audio_cid());
        draw(font, 38, 340, gray, "Underruns: %u   RTP missing: %u    tempo: %d/1000",
             g_underruns, g_seq_gaps, g_tempo_milli);
        draw(font, 38, 380, gray, "SBC failures: %u    overflow: %u   bad packets: %u",
             g_sbc_fail, g_overflow, g_bad_shape);
        draw(font, 38, 434, white, "On iPhone, select PlayStation Vita and play music.");
        draw(font, 38, 464, gray, "START = exit | Log: ux0:data/VitaPlay/sbc_playback.log");
        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceKernelDelayThread(100000); /* 10 fps GUI avoids CPU competition */
    }
    g_running = 0;
    if (tx_rc == 0) pthread_join(g_playback_thread, NULL);
    if (rx_rc == 0) pthread_join(g_receiver_thread, NULL);
    vita2d_free_pgf(font);
    vita2d_fini();
    if (g_log) fclose(g_log);
    sceKernelExitProcess(0);
    return 0;
}
