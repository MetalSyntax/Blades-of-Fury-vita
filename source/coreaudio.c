/**
 * @file coreaudio.c
 * @brief iOS AudioUnit RemoteIO and AudioSession emulation on top of sceAudioOut.
 * @details Handles guest audio output (such as embedded FMOD Ex or custom mixers):
 *          creates a RemoteIO unit, configures stream format and render callbacks,
 *          and streams audio on a dedicated high-priority sceAudioOut thread.
 *          Calls the guest callback via call_guest6.
 */
#include <stdio.h>
#include <AL/alc.h>
#include <AL/al.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "coreaudio.h"
#include "darwin.h"
#include "imports.h"
#include "log.h"
#include "macho.h"
#include "objc_rt.h"

#define kAudioUnitProperty_StreamFormat 8
#define kAudioUnitProperty_SetRenderCallback 23
#define kAudioFormatFlagIsFloat (1 << 0)
#define kAudioFormatFlagIsNonInterleaved (1 << 5)
#define kAudioFormatFlagsFixed824 (24 << 7) // kLinearPCMFormatFlagsSampleFractionShift
#define FOURCC(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#define GRAIN 512

typedef struct {
    double mSampleRate;
    uint32_t mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame,
        mChannelsPerFrame, mBitsPerChannel, mReserved;
} asbd;

typedef struct {
    uint32_t mNumberBuffers;
    struct {
        uint32_t mNumberChannels, mDataByteSize;
        void *mData;
    } mBuffers[2];
} buffer_list;

typedef struct {
    asbd fmt;
    void *proc, *refcon;
    SceUID thread;
    volatile int running;
} audio_unit;

static int ok(void) { return 0; }

static int au_FindNext(void *prev, const void *desc) { return prev ? 0 : 1; } // single audio component

static int au_InstanceNew(void *comp, audio_unit **out) {
    audio_unit *u = calloc(1, sizeof(*u));
    // RemoteIO default: 44.1 kHz, s16 interleaved stereo.
    u->fmt = (asbd){44100, FOURCC('l', 'p', 'c', 'm'), 0xC, 4, 1, 4, 2, 16, 0};
    *out = u;
    return 0;
}

static int au_SetProperty(audio_unit *u, uint32_t id, uint32_t scope, uint32_t elem, const void *data, uint32_t size) {
    if (id == kAudioUnitProperty_StreamFormat) {
        memcpy(&u->fmt, data, sizeof(asbd));
        l_info("AudioUnit: %d Hz, %u channels, %u bits, flags 0x%X", (int)u->fmt.mSampleRate,
               (unsigned)u->fmt.mChannelsPerFrame, (unsigned)u->fmt.mBitsPerChannel, (unsigned)u->fmt.mFormatFlags);
    } else if (id == kAudioUnitProperty_SetRenderCallback) {
        u->proc = ((void *const *)data)[0];
        u->refcon = ((void *const *)data)[1];
    }
    return 0;
}

static int au_GetProperty(audio_unit *u, uint32_t id, uint32_t scope, uint32_t elem, void *data, uint32_t *size) {
    if (id == kAudioUnitProperty_StreamFormat) {
        memcpy(data, &u->fmt, sizeof(asbd));
        *size = sizeof(asbd);
        return 0;
    }
    memset(data, 0, *size);
    return 0;
}

static int16_t to_s16(const audio_unit *u, const uint8_t *p) {
    uint32_t fl = u->fmt.mFormatFlags;
    if (fl & kAudioFormatFlagIsFloat) {
        float f = *(const float *)p * 32767.0f;
        return f > 32767 ? 32767 : f < -32768 ? -32768 : (int16_t)f;
    }
    if (u->fmt.mBitsPerChannel == 32) { // 8.24 fixed-point (canonical AudioUnit) or s32
        int32_t v = *(const int32_t *)p >> ((fl & kAudioFormatFlagsFixed824) ? 9 : 16);
        return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
    }
    return *(const int16_t *)p;
}

static int audio_thread(SceSize args, void *argp) {
    audio_unit *u = *(audio_unit **)argp;
    int rate = (int)u->fmt.mSampleRate, ch = u->fmt.mChannelsPerFrame;
    int bytes = u->fmt.mBitsPerChannel / 8;
    int nonint = (u->fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) && ch == 2;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, rate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        l_error("sceAudioOutOpenPort(%d Hz) = 0x%08X", rate, port);
        return 0;
    }
    uint8_t *raw = malloc(GRAIN * 2 * 4);
    int16_t out[GRAIN * 2];
    uint8_t ts[64] = {0}; // AudioTimeStamp
    uint32_t flags = 0;
    double sample_time = 0;
    buffer_list bl;
    while (u->running) {
        if (nonint)
            bl = (buffer_list){2, {{1, GRAIN * bytes, raw}, {1, GRAIN * bytes, raw + GRAIN * 4}}};
        else
            bl = (buffer_list){1, {{ch, GRAIN * ch * bytes, raw}}};
        memcpy(ts, &sample_time, 8);
        ((uint32_t *)ts)[14] = 1; // mFlags = kAudioTimeStampSampleTimeValid
        flags = 0;
        call_guest6(u->proc, (uint32_t)u->refcon, (uint32_t)&flags, (uint32_t)ts, 0, GRAIN, (uint32_t)&bl);
        sample_time += GRAIN;
        for (int i = 0; i < GRAIN; i++) {
            if (nonint) {
                out[i * 2] = to_s16(u, raw + i * bytes);
                out[i * 2 + 1] = to_s16(u, raw + GRAIN * 4 + i * bytes);
            } else {
                out[i * 2] = to_s16(u, raw + i * ch * bytes);
                out[i * 2 + 1] = ch == 2 ? to_s16(u, raw + (i * 2 + 1) * bytes) : out[i * 2];
            }
        }
        sceAudioOutOutput(port, out);
    }
    sceAudioOutReleasePort(port);
    free(raw);
    return 0;
}

static int au_Start(audio_unit *u) {
    if (u->running || !u->proc)
        return 0;
    u->running = 1;
    u->thread = sceKernelCreateThread("coreaudio", audio_thread, 0x10000100, 256 * 1024, 0, 0, NULL);
    sceKernelStartThread(u->thread, sizeof(u), &u);
    return 0;
}

static int au_Stop(audio_unit *u) {
    if (u->running) {
        u->running = 0;
        sceKernelWaitThreadEnd(u->thread, NULL, NULL);
        sceKernelDeleteThread(u->thread);
    }
    return 0;
}

static int au_Dispose(audio_unit *u) {
    au_Stop(u);
    free(u);
    return 0;
}

static int au_Render(void) { return -1; } // audio input (microphone): not supported

// AudioSession: queried by audio engines to select stream properties.
static int as_GetProperty(uint32_t id, uint32_t *size, void *data) {
    memset(data, 0, *size);
    switch (id) {
    case FOURCC('c', 'h', 's', 'r'): // CurrentHardwareSampleRate (Float64)
        *(double *)data = 44100.0;
        break;
    case FOURCC('c', 'h', 'o', 'c'): // CurrentHardwareOutputNumberChannels
        *(uint32_t *)data = 2;
        break;
    case FOURCC('c', 'h', 'b', 'd'): // CurrentHardwareIOBufferDuration (Float32)
        *(float *)data = (float)GRAIN / 44100.0f;
        break;
    case FOURCC('c', 'h', 'o', 'l'): // CurrentHardwareOutputLatency (Float32)
        *(float *)data = 0.012f;
        break;
    default:
        l_debug("AudioSessionGetProperty('%c%c%c%c') -> 0", id >> 24, (id >> 16) & 0xFF, (id >> 8) & 0xFF, id & 0xFF);
    }
    return 0;
}

// ---------------------------------------------------------------- AudioFile (WAV parser for SFX)

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t data_size;
    uint32_t data_offset;
    FILE *fp;
} wav_file_t;

static int wav_open(const char *path, wav_file_t *w) {
    if (!path || !w)
        return -1;
    memset(w, 0, sizeof(*w));

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    char id[4];
    uint32_t size;
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "RIFF", 4) != 0) {
        fclose(f);
        return -1;
    }
    fseek(f, 4, SEEK_CUR); // skip RIFF chunk size
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "WAVE", 4) != 0) {
        fclose(f);
        return -1;
    }

    int found_fmt = 0, found_data = 0;
    while (!feof(f)) {
        if (fread(id, 1, 4, f) != 4 || fread(&size, 4, 1, f) != 1)
            break;
        long chunk_start = ftell(f);
        if (memcmp(id, "fmt ", 4) == 0 && size >= 16) {
            uint16_t tag = 0, ch = 0, bits = 0;
            uint32_t rate = 0;
            fread(&tag, 2, 1, f);
            fread(&ch, 2, 1, f);
            fread(&rate, 4, 1, f);
            fseek(f, 6, SEEK_CUR); // skip nAvgBytesPerSec (4), nBlockAlign (2)
            fread(&bits, 2, 1, f);
            w->channels = ch;
            w->sample_rate = rate;
            w->bits_per_sample = bits;
            found_fmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            w->data_offset = (uint32_t)chunk_start;
            w->data_size = size;
            found_data = 1;
            break;
        }
        fseek(f, chunk_start + size, SEEK_SET);
    }

    if (!found_fmt || !found_data) {
        fclose(f);
        return -1;
    }

    w->fp = f;
    return 0;
}

typedef struct {
    wav_file_t wav;
    asbd fmt;
} audio_file_ctx;

static const char *get_path_from_url(void *url) {
    if (!url)
        return NULL;
    const char *p = (const char *)url;
    if (p[0] == '/' || !strncmp(p, "ux0:", 4) || !strncmp(p, "app0:", 5))
        return p;
    return ns_cstr((id)url);
}

static int my_AudioFileOpenURL(void *url, int permissions, int hint, void **out) {
    if (!url || !out)
        return -1;
    const char *raw_path = get_path_from_url(url);
    if (!raw_path || !raw_path[0])
        return -1;

    char rpath[256];
    darwin_path(raw_path, rpath, sizeof(rpath));

    audio_file_ctx *af = calloc(1, sizeof(*af));
    if (!af)
        return -1;

    if (wav_open(rpath, &af->wav) < 0) {
        char alt[256];
        snprintf(alt, sizeof(alt), "%s", rpath);
        char *dot = strrchr(alt, '.');
        if (dot && !strcasecmp(dot, ".m4a")) {
            strcpy(dot, ".wav");
            if (wav_open(alt, &af->wav) < 0) {
                free(af);
                return -1;
            }
        } else {
            free(af);
            return -1;
        }
    }

    af->fmt.mSampleRate = (double)af->wav.sample_rate;
    af->fmt.mFormatID = FOURCC('l', 'p', 'c', 'm');
    af->fmt.mFormatFlags = 0xC; // signed int, native endian
    af->fmt.mBytesPerPacket = af->wav.channels * (af->wav.bits_per_sample / 8);
    af->fmt.mFramesPerPacket = 1;
    af->fmt.mBytesPerFrame = af->fmt.mBytesPerPacket;
    af->fmt.mChannelsPerFrame = af->wav.channels;
    af->fmt.mBitsPerChannel = af->wav.bits_per_sample;
    af->fmt.mReserved = 0;

    *out = af;
    return 0; // noErr
}

static int my_AudioFileClose(void *af_ptr) {
    if (!af_ptr)
        return -1;
    audio_file_ctx *af = (audio_file_ctx *)af_ptr;
    if (af->wav.fp)
        fclose(af->wav.fp);
    free(af);
    return 0;
}

static int my_AudioFileGetProperty(void *af_ptr, uint32_t prop, uint32_t *ioSize, void *outData) {
    if (!af_ptr || !outData || !ioSize)
        return -1;
    audio_file_ctx *af = (audio_file_ctx *)af_ptr;

    if (prop == FOURCC('d', 'f', 'm', 't')) { // kAudioFilePropertyDataFormat
        uint32_t sz = sizeof(asbd);
        if (*ioSize < sz)
            sz = *ioSize;
        memcpy(outData, &af->fmt, sz);
        *ioSize = sz;
        return 0;
    } else if (prop == FOURCC('b', 'c', 'n', 't')) { // kAudioFilePropertyAudioDataByteCount
        uint64_t cnt = (uint64_t)af->wav.data_size;
        uint32_t sz = sizeof(uint64_t);
        if (*ioSize < sz)
            sz = *ioSize;
        memcpy(outData, &cnt, sz);
        *ioSize = sz;
        return 0;
    }
    return -1;
}

static int my_AudioFileGetPropertyInfo(void *af_ptr, uint32_t prop, uint32_t *outSize, uint32_t *outWritable) {
    if (!af_ptr)
        return -1;
    if (outWritable)
        *outWritable = 0;
    if (prop == FOURCC('d', 'f', 'm', 't')) {
        if (outSize)
            *outSize = sizeof(asbd);
        return 0;
    } else if (prop == FOURCC('b', 'c', 'n', 't')) {
        if (outSize)
            *outSize = sizeof(uint64_t);
        return 0;
    }
    return -1;
}

static int my_AudioFileReadBytes(void *af_ptr, int useCache, int64_t startingByte, uint32_t *ioNumBytes, void *outBuf) {
    if (!af_ptr || !outBuf || !ioNumBytes)
        return -1;
    audio_file_ctx *af = (audio_file_ctx *)af_ptr;
    if (!af->wav.fp)
        return -1;

    fseek(af->wav.fp, af->wav.data_offset + (long)startingByte, SEEK_SET);
    size_t rd = fread(outBuf, 1, *ioNumBytes, af->wav.fp);
    *ioNumBytes = (uint32_t)rd;
    return 0;
}

// ---------------------------------------------------------------- BGM (OpenAL background music stream)

static ALuint bgm_source = 0;
static ALuint bgm_buffer = 0;
static float s_bgm_vol = 1.0f;
static int s_bgm_loop = 1;

int sound_bgm_load(const char *path, int loop, int crossfade) {
    if (!path || !path[0])
        return -1;

    s_bgm_loop = loop;

    char rpath[256];
    darwin_path(path, rpath, sizeof(rpath));

    char wav_path[256];
    snprintf(wav_path, sizeof(wav_path), "%s", rpath);
    char *dot = strrchr(wav_path, '.');
    if (dot && !strcasecmp(dot, ".m4a")) {
        strcpy(dot, ".wav");
    }

    wav_file_t w;
    const char *opened_path = wav_path;
    if (wav_open(wav_path, &w) < 0) {
        if (wav_open(rpath, &w) < 0) {
            l_warn("BGM: could not open '%s' (nor '%s')", wav_path, rpath);
            return -1;
        }
        opened_path = rpath;
    }

    void *pcm = malloc(w.data_size);
    if (!pcm) {
        fclose(w.fp);
        return -1;
    }

    fseek(w.fp, w.data_offset, SEEK_SET);
    fread(pcm, 1, w.data_size, w.fp);
    fclose(w.fp);

    ALenum fmt = AL_FORMAT_MONO16;
    if (w.channels == 2 && w.bits_per_sample == 16)
        fmt = AL_FORMAT_STEREO16;
    else if (w.channels == 1 && w.bits_per_sample == 16)
        fmt = AL_FORMAT_MONO16;
    else if (w.channels == 2 && w.bits_per_sample == 8)
        fmt = AL_FORMAT_STEREO8;
    else if (w.channels == 1 && w.bits_per_sample == 8)
        fmt = AL_FORMAT_MONO8;

    if (!bgm_source) {
        alGenSources(1, &bgm_source);
    } else {
        alSourceStop(bgm_source);
        alSourcei(bgm_source, AL_BUFFER, 0);
    }

    if (bgm_buffer) {
        alDeleteBuffers(1, &bgm_buffer);
        bgm_buffer = 0;
    }

    alGenBuffers(1, &bgm_buffer);
    alBufferData(bgm_buffer, fmt, pcm, w.data_size, w.sample_rate);
    free(pcm);

    alSourcei(bgm_source, AL_BUFFER, bgm_buffer);
    alSourcei(bgm_source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    alSourcef(bgm_source, AL_GAIN, s_bgm_vol);

    l_info("BGM loaded '%s' (%u Hz, ch=%u, sz=%u B) -> buffer %u",
           opened_path, w.sample_rate, w.channels, w.data_size, bgm_buffer);
    return 0;
}

int sound_bgm_start(int loop) {
    if (bgm_source) {
        alSourcei(bgm_source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
        alSourcePlay(bgm_source);
        l_info("BGM started (loop=%d)", loop);
    }
    return 0;
}

int sound_bgm_stop(int all) {
    if (bgm_source) {
        alSourceStop(bgm_source);
        l_info("BGM stopped");
    }
    return 0;
}

int sound_bgm_unload(void) {
    if (bgm_source) {
        alSourceStop(bgm_source);
        alSourcei(bgm_source, AL_BUFFER, 0);
    }
    if (bgm_buffer) {
        alDeleteBuffers(1, &bgm_buffer);
        bgm_buffer = 0;
    }
    l_info("BGM unloaded");
    return 0;
}

int sound_bgm_set_volume(float vol) {
    s_bgm_vol = vol;
    if (bgm_source) {
        alSourcef(bgm_source, AL_GAIN, vol);
    }
    return 0;
}

// ---------------------------------------------------------------- OpenAL (openal-soft from VitaSDK)

// Apple extension: buffer data stays owned by the app. openal-soft copies it, which is equivalent
// as long as the game does not rewrite the memory afterwards.
static void al_BufferDataStatic(ALuint buf, ALenum fmt, const ALvoid *data, ALsizei size, ALsizei freq) {
    alBufferData(buf, fmt, data, size, freq);
}

static int audio_fail(void) { return -1; }

// iOS OpenAL returns AL_NO_ERROR before a context is current, whereas openal-soft
// returns AL_INVALID_OPERATION (0xA004). SoundEngine checks alGetError() right after alcOpenDevice.
static ALenum wrap_alGetError(void) {
    if (!alcGetCurrentContext())
        return AL_NO_ERROR;
    return alGetError();
}

// Extensions are returned from our own import table so guest calls enter through ml_align_call.
static void *al_GetProcAddress(ALCdevice *dev, const ALCchar *name) {
    char n[64];
    snprintf(n, sizeof(n), "_%s", name);
    void *p = (void *)macho_resolve(n);
    l_info("alcGetProcAddress(%s) -> %p", name, p);
    return p;
}

#define F(name, fn) {"_" #name, (uintptr_t)(fn)}
#define D(name) {"_" #name, (uintptr_t)(name)}

const import_entry coreaudio_imports[] = {
    F(AudioComponentFindNext, au_FindNext), F(AudioComponentInstanceNew, au_InstanceNew),
    F(AudioComponentInstanceDispose, au_Dispose), F(AudioUnitSetProperty, au_SetProperty),
    F(AudioUnitGetProperty, au_GetProperty), F(AudioUnitInitialize, ok), F(AudioUnitUninitialize, ok),
    F(AudioOutputUnitStart, au_Start), F(AudioOutputUnitStop, au_Stop), F(AudioUnitRender, au_Render),
    F(AudioSessionInitialize, ok), F(AudioSessionSetActive, ok), F(AudioSessionSetProperty, ok),
    F(AudioSessionGetProperty, as_GetProperty), F(AudioSessionAddPropertyListener, ok),
    // imported via __nl_symbol_ptr (function pointer): requires code stub, not data
    F(AudioSessionRemovePropertyListenerWithUserData, ok),
    // AudioFile API: parses WAV assets into PCM for OpenAL SoundEngine
    F(AudioFileOpenURL, my_AudioFileOpenURL), F(AudioFileClose, my_AudioFileClose),
    F(AudioFileGetProperty, my_AudioFileGetProperty), F(AudioFileGetPropertyInfo, my_AudioFileGetPropertyInfo),
    F(AudioFileReadBytes, my_AudioFileReadBytes), F(AudioFileReadPackets, audio_fail),
    // AudioQueue stubs: BGM is handled via sound_bgm_* OpenAL hooks
    F(AudioQueueNewOutput, audio_fail), F(AudioQueueAllocateBuffer, audio_fail), F(AudioQueueEnqueueBuffer, audio_fail),
    F(AudioQueueStart, audio_fail), F(AudioQueuePrime, audio_fail), F(AudioQueueStop, audio_fail),
    F(AudioQueueDispose, audio_fail), F(AudioQueueSetParameter, audio_fail), F(AudioQueueSetProperty, audio_fail),
    F(AudioQueueAddPropertyListener, audio_fail),
    D(alBufferData), D(alDeleteBuffers), D(alDeleteSources), D(alDistanceModel), D(alGenBuffers), D(alGenSources),
    F(alGetError, wrap_alGetError), D(alGetSourcei), D(alListener3f), D(alListenerfv), D(alSource3f), D(alSourcePause), D(alSourcePlay),
    D(alSourceQueueBuffers), D(alSourceStop), D(alSourceUnqueueBuffers), D(alSourcef), D(alSourcefv),
    D(alSourcei), D(alcCloseDevice), D(alcCreateContext), D(alcDestroyContext),
    D(alcMakeContextCurrent), D(alcOpenDevice), D(alcProcessContext), D(alcSuspendContext),
    F(alcGetProcAddress, al_GetProcAddress), F(alBufferDataStatic, al_BufferDataStatic),
};
const int coreaudio_imports_count = sizeof(coreaudio_imports) / sizeof(coreaudio_imports[0]);
