#!/usr/bin/env python3
"""Source-only VitaPlay audio v4 patch. No secrets or compiled binary edits."""
from pathlib import Path

root = Path("upstream/vitaplay")
def replace_one(path: Path, before: str, after: str) -> None:
    contents = path.read_text()
    matches = contents.count(before)
    if matches != 1:
        raise SystemExit(f"{path}: expected 1 occurrence, got {matches}: {before[:80]}")
    path.write_text(contents.replace(before, after))

info = root / "src/info.c"
replace_one(info, "#define AUDIO_PCM 0xcffc", "#define AUDIO_PCM 0xc3fc")
replace_one(info, "#define AUDIO_PCM_MONO 0x4554", "#define AUDIO_PCM_MONO 0x4154")
assert "format(102, \"media\", AUDIO_AAC_LC)" in info.read_text()
assert "latency(102, \"default\")" in info.read_text()
print("Audio v4: 48 kHz PCM and AAC-LC media 102 both advertised.")

makefile=root/"Makefile"
replace_one(makefile, "-lSceAudio_stub \\", "-lSceAudiodec_stub -lSceAudio_stub \\")

audio=root/"src/audio.c"
replace_one(audio, "#include <psp2/audioout.h>", "#include <psp2/audioout.h>\n#include <psp2/audiodec.h>\n#include <pthread.h>")
replace_one(audio,
    " * AAC-LC is not decoded yet (it would need sceAudiodec); such a stream is accepted and logged so a\n * test shows whether the phone actually negotiates it. PCM and Opus play.",
    " * Wireless CarPlay media uses AAC-LC. Decode it using the native SceAudiodec library.\n * Keep PCM and Opus paths unchanged.")
aac_code=r'''
/* One decoder per stream RX thread, initialized only when an AAC packet arrives.
 * Native AAC decode is not invoked by screen-only CarPlay sessions.
 * The Vita's native decoder supports raw AAC and ADTS input, selected from the first packet.
 */
typedef struct {
    SceAudiodecCtrl ctrl;
    SceAudiodecInfo info;
    uint8_t compressed[SCE_AUDIODEC_AAC_MAX_ES_SIZE] __attribute__((aligned(0x100)));
    int16_t decoded[SCE_AUDIODEC_AAC_MAX_SAMPLES * 2] __attribute__((aligned(0x100)));
    bool created;
    bool disabled;
    unsigned errors;
} aac_decoder;

static pthread_once_t g_aac_once = PTHREAD_ONCE_INIT;
static int g_aac_library_rc = -1;
static void aac_init_library_once(void) {
    SceAudiodecInitParam init = {0};
    init.aac.size = sizeof(init.aac);
    init.aac.totalStreams = 2;
    g_aac_library_rc = sceAudiodecInitLibrary(SCE_AUDIODEC_TYPE_AAC, &init);
    vp_log("native AAC library initialization rc=0x%08x", (unsigned)g_aac_library_rc);
}

static void decode_aac(audio_stream *s, aac_decoder *d, const uint8_t *data, int bytes) {
    if (d->disabled || bytes <= 0) return;
    if (bytes > SCE_AUDIODEC_AAC_MAX_ES_SIZE) {
        if (++d->errors <= 3) vp_log("AAC packet too large: %d", bytes);
        return;
    }
    if (!d->created) {
        pthread_once(&g_aac_once, aac_init_library_once);
        if (g_aac_library_rc < 0 &&
            g_aac_library_rc != (int)SCE_AUDIODEC_ERROR_ALREADY_INITIALIZED) {
            vp_log("AAC disabled: native library rc=0x%08x", (unsigned)g_aac_library_rc);
            d->disabled = true;
            return;
        }
        d->ctrl.size = sizeof(d->ctrl);
        d->ctrl.pEs = d->compressed;
        d->ctrl.maxEsSize = sizeof(d->compressed);
        d->ctrl.pPcm = d->decoded;
        d->ctrl.maxPcmSize = sizeof(d->decoded);
        d->ctrl.wordLength = SCE_AUDIODEC_WORD_LENGTH_16BITS;
        d->ctrl.pInfo = &d->info;
        d->info.aac.size = sizeof(d->info.aac);
        d->info.aac.isAdts = bytes > 1 && data[0] == 0xff && (data[1] & 0xf0) == 0xf0;
        d->info.aac.ch = 2;
        d->info.aac.samplingRate = s->src_rate > 0 ? s->src_rate : 48000;
        d->info.aac.isSbr = 0;
        int rc = sceAudiodecCreateDecoder(&d->ctrl, SCE_AUDIODEC_TYPE_AAC);
        if (rc < 0) {
            vp_log("AAC decoder create failed rc=0x%08x adts=%u", (unsigned)rc, d->info.aac.isAdts);
            d->disabled = true;
            return;
        }
        d->created = true;
        vp_log("native AAC decoder ready rate=%u ch=%u ADTS=%u",
               d->info.aac.samplingRate, d->info.aac.ch, d->info.aac.isAdts);
    }
    memcpy(d->compressed, data, bytes);
    d->ctrl.pEs = d->compressed;
    d->ctrl.inputEsSize = bytes;
    d->ctrl.maxEsSize = sizeof(d->compressed);
    d->ctrl.pPcm = d->decoded;
    d->ctrl.outputPcmSize = 0;
    d->ctrl.maxPcmSize = sizeof(d->decoded);
    int rc = sceAudiodecDecode(&d->ctrl);
    if (rc < 0) {
        if (++d->errors <= 4 || (d->errors % 256) == 0)
            vp_log("AAC decode failed rc=0x%08x packet=%u", (unsigned)rc, s->packets);
        return;
    }
    int channels = d->info.aac.ch == 1 ? 1 : 2;
    int samples = (int)(d->ctrl.outputPcmSize / (sizeof(int16_t) * channels));
    if (samples < 1 || samples > SCE_AUDIODEC_AAC_MAX_SAMPLES) {
        if (++d->errors <= 4)
            vp_log("AAC output length invalid: %u bytes ch=%d", d->ctrl.outputPcmSize, channels);
        return;
    }
    if (s->packets <= 3)
        vp_log("AAC decoded packet=%u frames=%d", s->packets, samples);
    if (channels == 2) {
        resample_push(s, d->decoded, samples);
    } else {
        int16_t stereo[SCE_AUDIODEC_AAC_MAX_SAMPLES * 2];
        for (int i = 0; i < samples; ++i)
            stereo[i * 2] = stereo[i * 2 + 1] = d->decoded[i];
        resample_push(s, stereo, samples);
    }
}
'''
replace_one(audio, "/* ---- UDP receive ---- */", aac_code + "\n/* ---- UDP receive ---- */")
replace_one(audio,
    "    uint8_t plain[DATAGRAM_BYTES];\n    while (s->active) {",
    "    uint8_t plain[DATAGRAM_BYTES];\n    aac_decoder aac __attribute__((aligned(0x100)));\n    memset(&aac, 0, sizeof(aac));\n    while (s->active) {")
replace_one(audio,
    '        case CODEC_AAC:\n            if (s->packets == 1) vp_log("audio type=%d is AAC-LC; not decoded yet (needs sceAudiodec)", s->type);\n            break;',
    "        case CODEC_AAC: decode_aac(s, &aac, plain, plen); break;")
replace_one(audio,
    "    }\n    return NULL;\n}\n\n/* ---- sceAudioOut playback:",
    "    }\n    if (aac.created) sceAudiodecDeleteDecoder(&aac.ctrl);\n    return NULL;\n}\n\n/* ---- sceAudioOut playback:")

assert "decode_aac(s, &aac, plain, plen)" in audio.read_text()
assert "-lSceAudiodec_stub" in makefile.read_text()
print("Audio v4: native AAC decoder integrated and linked.")
