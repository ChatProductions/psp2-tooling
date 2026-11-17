/*
 * VitaPlay v9 hybrid receiver: Bluetooth A2DP SBC music + Wi-Fi CarPlay video.
 *
 * Uses the matching VitaPlayBt kernel plugin's RTP queue; the same SceBt RX
 * hook also handles the original iAP2 bootstrap. No second taiHEN plugin.
 *
 * Google libsbc is Apache-2.0. Only process authenticated RTP/SBC framing
 * observed in the user's standalone A2DP test. This does not claim HFP,
 * microphone, Siri, or Wi-Fi CarPlay audio support.
 */
#include "btaudio.h"
#include "common.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <sbc.h>

extern int vpbt_audio_read(void *buffer, int capacity);
extern int vpbt_audio_packets(void);
extern int vpbt_audio_dropped(void);
extern int vpbt_audio_cid(void);
extern int vpbt_audio_enable(int enable);

#define HYBRID_GRAIN 1024u
#define HYBRID_RATE 44100
#define HYBRID_RING 32768u
#define HYBRID_MASK (HYBRID_RING - 1u)
#define HYBRID_WARMUP (12u * HYBRID_GRAIN) /* ~279ms */
#define HYBRID_RECOVER (6u * HYBRID_GRAIN) /* ~139ms */
#define HYBRID_MAX_BUFFER (26u * HYBRID_GRAIN) /* ~604ms */
#define HYBRID_MAX_PACKET 1200
#define HYBRID_FADE 96u

static int16_t g_pcm[HYBRID_RING*2u];
static volatile unsigned g_head, g_tail;
static volatile unsigned g_received, g_decoded, g_bad, g_lost_seq, g_overflows;
static volatile unsigned g_underruns, g_silence, g_stale, g_played;
static volatile int g_port = -1;
static volatile int g_started;
static volatile int g_rx_started;
static volatile int g_output_started;
static volatile int g_drop_base;

static void *hybrid_rx(void *unused) {
    (void)unused;
    unsigned char pkt[HYBRID_MAX_PACKET];
    int16_t pcm[HYBRID_GRAIN * 2u];
    sbc_t decoder;
    sbc_reset(&decoder);

    /* Native kernel plugin persists after an app restart; don't play old data. */
    for (int i=0;i<256;i++) {
        int n=vpbt_audio_read(pkt,sizeof(pkt));
        if(n<=0) break;
        ++g_stale;
    }
    g_drop_base=vpbt_audio_dropped();
    vp_log("v9 BT SBC RX ready; stale=%u kernel drop baseline=%d",
           g_stale,g_drop_base);
    g_rx_started=1;
    int have_seq=0;
    uint16_t prev_seq=0;

    for (;;) {
        int n=vpbt_audio_read(pkt,sizeof(pkt));
        if(n==0) {
            sceKernelDelayThread(2500);
            continue;
        }
        if(n<0) {
            if(g_bad++<4) vp_log("v9 BT RTP syscall error=%d",n);
            sceKernelDelayThread(200000);
            continue;
        }
        ++g_received;
        if (n < 17 || pkt[0] != 0x80 || (pkt[1]&0x7f)!=96 ||
            (pkt[12]&0x0f)!=8 || pkt[13]!=0x9c) {
            if(g_bad++<4) vp_log("v9 BT unsupported RTP packet len=%d",n);
            continue;
        }

        uint16_t seq=(uint16_t)(((unsigned)pkt[2]<<8)|pkt[3]);
        if (have_seq) {
            unsigned delta=(uint16_t)(seq-prev_seq);
            if(delta>1 && delta<0x8000u) g_lost_seq += delta-1;
        }
        prev_seq=seq;
        have_seq=1;

        struct sbc_frame shape;
        int probe=sbc_probe(pkt+13,&shape);
        unsigned size=probe==0 ? sbc_get_frame_size(&shape):0;
        if(probe!=0 || size==0 || size>150 || shape.freq!=SBC_FREQ_44K1 ||
           shape.nblocks!=16 || shape.nsubbands!=8 ||
           shape.mode!=SBC_MODE_JOINT_STEREO || (unsigned)n<13u+8u*size) {
            if(g_bad++<4)
                vp_log("v9 BT SBC format rejected probe=%d flen=%u rtp=%d",probe,size,n);
            continue;
        }

        int good=1;
        for(unsigned i=0;i<8;i++) {
            struct sbc_frame current;
            int16_t *out=pcm+(i*128u*2u);
            if(sbc_decode(&decoder,pkt+13+i*size,size,&current,out,2,out+1,2)!=0) {
                good=0;
                if(g_bad++<4) vp_log("v9 BT SBC decoding error at frame=%u",i);
                break;
            }
        }
        if(!good) continue;
        ++g_decoded;

        unsigned head=g_head;
        unsigned tail=g_tail;
        if(head-tail>=HYBRID_MAX_BUFFER) {
            ++g_overflows;
            continue; /* avoid lagging seconds behind iPhone */
        }
        unsigned offset=(head&HYBRID_MASK)*2u;
        /* g_head is always HYBRID_GRAIN-aligned and the 32K ring divides it. */
        memcpy(g_pcm+offset,pcm,sizeof(pcm));
        __sync_synchronize();
        g_head=head+HYBRID_GRAIN;
        if((g_received % 256u)==0)
            vp_log("v9 BT RX packets=%u decoded=%u RTP missing=%u new kernel drops=%d overflow=%u",
                   g_received,g_decoded,g_lost_seq,
                   vpbt_audio_dropped()-g_drop_base,g_overflows);
    }
    return NULL;
}

static void *hybrid_out(void *unused) {
    (void)unused;
    int16_t block[HYBRID_GRAIN*2u];
    int port=-1;

    /* Never contend for speakers before actual Bluetooth music arrives.
     * Pairing, iAP2 handoff and Wi-Fi video do not require opening a port.
     */
    while(g_head-g_tail < HYBRID_WARMUP)
        sceKernelDelayThread(5000);

    port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,HYBRID_GRAIN,
                             HYBRID_RATE,SCE_AUDIO_OUT_MODE_STEREO);
    g_port=port;
    if(port<0) {
        vp_log("v9 BT audio BGM port open failed rc=0x%08x",(unsigned)port);
        return NULL;
    }
    g_output_started=1;
    vp_log("v9 HYBRID PLAYBACK START: Bluetooth SBC 44100Hz stereo + CarPlay Wi-Fi video; port=%d",port);

    unsigned last_log=0;
    unsigned dropouts=0;
    int rebuffer=0;
    unsigned held=0;
    int16_t old_last_l=0,old_last_r=0;
    for (;;) {
        unsigned tail=g_tail;
        __sync_synchronize();
        unsigned head=g_head;
        unsigned available=head-tail;
        int have=available>=HYBRID_GRAIN;
        if(rebuffer && available<HYBRID_RECOVER) have=0;

        if(have) {
            unsigned offset=(tail&HYBRID_MASK)*2u;
            memcpy(block,g_pcm+offset,sizeof(block));
            __sync_synchronize();
            g_tail=tail+HYBRID_GRAIN;
            if(held) {
                for(unsigned i=0;i<HYBRID_FADE;i++) {
                    int factor=(int)((i+1u)*256u/HYBRID_FADE);
                    block[2u*i]=(int16_t)((int32_t)block[2u*i]*factor/256);
                    block[2u*i+1]=(int16_t)((int32_t)block[2u*i+1]*factor/256);
                }
            }
            old_last_l=block[(HYBRID_GRAIN-1u)*2u];
            old_last_r=block[(HYBRID_GRAIN-1u)*2u+1u];
            rebuffer=0;
            held=0;
        } else {
            if(!rebuffer) {
                ++g_underruns;
                ++dropouts;
                rebuffer=1;
                if(dropouts<=8)
                    vp_log("v9 BT audio underrun FIFO=%u; refill=%u",
                           available,HYBRID_RECOVER);
            }
            ++g_silence;
            ++held;
            if(held==1) {
                /* Brief fade of last frame to zero, not repeated music. */
                for(unsigned i=0;i<HYBRID_GRAIN;i++) {
                    int scale=i<HYBRID_FADE ?
                        (int)((HYBRID_FADE-i)*256u/HYBRID_FADE):0;
                    block[2u*i]=(int16_t)((int32_t)old_last_l*scale/256);
                    block[2u*i+1]=(int16_t)((int32_t)old_last_r*scale/256);
                }
            } else memset(block,0,sizeof(block));
        }
        int rc=sceAudioOutOutput(port,block);
        ++g_played;
        if(rc<0 && g_played<=5u) vp_log("v9 BT sceAudioOutOutput error=0x%08x",(unsigned)rc);
        if(g_played-last_log>=128u) {
            vp_log("v9 BT SOUND blocks=%u buf_ms=%u rx=%u decoded=%u "
                   "new_kernel_drops=%d missing_rtp=%u underflows=%u silences=%u overflow=%u port=%d",
                   g_played,(g_head-g_tail)*1000u/HYBRID_RATE,
                   g_received,g_decoded,vpbt_audio_dropped()-g_drop_base,
                   g_lost_seq,g_underruns,g_silence,g_overflows,g_port);
            last_log=g_played;
        }
    }
    return NULL;
}

void bt_audio_start(void) {
    if(g_started) return;
    g_started=1;
    vp_log("v10 hybrid mode: CarPlay video already streaming; enabling Bluetooth SBC capture");
    int enabled=vpbt_audio_enable(1);
    vp_log("v10 kernel Bluetooth SBC capture enabled=%d",enabled);
    if (enabled != 1) return;
    if(spawn_thread(hybrid_rx,NULL)!=0)
        vp_log("v9 BT receiver thread creation failed");
    else if(spawn_thread(hybrid_out,NULL)!=0)
        vp_log("v9 BT output thread creation failed");
}
