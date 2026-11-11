/**
 * @file movie.c
 * @brief MPMoviePlayerController playback of the original .m4v with FFmpeg (software H.264 + AAC).
 *
 * Uses the minimal FFmpeg built in vendor/ffmpeg (mov demuxer, h264/aac decoders): vitasdk's ffmpeg
 * package ships neither. Blocking: -[MPMoviePlayerController play] runs the whole clip, as the
 * fullscreen iOS player does. Frames go to a YUV420P texture shown by gles_present's movie path;
 * audio is resampled to S16 and fed to sceAudioOut from a ring by its own thread. X / O / START skip.
 */
#include <stdlib.h>
#include <string.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vitaGL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

#include "gles.h"
#include "log.h"

#define GRAIN 1024
#define RING (48000 * 2 * 2) // ~1 s of S16 stereo
static int16_t ring[RING];
static volatile unsigned ring_w, ring_r; // in samples (int16), free-running
static volatile int audio_run;
static int audio_rate;

static int audio_thread(SceSize args, void *argp) {
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, GRAIN, audio_rate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        l_warn("movie: sceAudioOutOpenPort(%d) = 0x%08X", audio_rate, port);
        return sceKernelExitDeleteThread(0);
    }
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    static int16_t out[GRAIN * 2];
    while (audio_run) {
        for (int i = 0; i < GRAIN * 2; i++)
            out[i] = ring_r != ring_w ? ring[ring_r++ % RING] : 0;
        sceAudioOutOutput(port, out);
    }
    sceAudioOutReleasePort(port);
    return sceKernelExitDeleteThread(0);
}

static int open_dec(AVFormatContext *fmt, enum AVMediaType type, AVCodecContext **out) {
    const AVCodec *codec;
    int idx = av_find_best_stream(fmt, type, -1, -1, &codec, 0);
    if (idx < 0)
        return -1;
    AVCodecContext *c = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(c, fmt->streams[idx]->codecpar);
    c->thread_count = 1; // log_023: h264 frame-thread workers crashed (data abort); 480x270 needs no threads
    if (avcodec_open2(c, codec, NULL) < 0) {
        avcodec_free_context(&c);
        return -1;
    }
    *out = c;
    return idx;
}

void movie_play_file(const char *path) {
    char url[300];
    snprintf(url, sizeof(url), "file:%s", path); // "ux0:" would otherwise parse as a protocol
    AVFormatContext *fmt = NULL;
    AVCodecContext *vc = NULL, *ac = NULL;
    int r = avformat_open_input(&fmt, url, NULL, NULL);
    if (r < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
        l_warn("movie: cannot open '%s' (%d)", path, r);
        goto out;
    }
    int vi = open_dec(fmt, AVMEDIA_TYPE_VIDEO, &vc), ai = open_dec(fmt, AVMEDIA_TYPE_AUDIO, &ac);
    if (vi < 0) {
        l_warn("movie: no video decoder");
        goto out;
    }
    l_info("movie: playing '%s' %dx%d, audio %d Hz", path, vc->width, vc->height, ac ? ac->sample_rate : 0);

    SwrContext *swr = NULL;
    SceUID th = -1;
    if (ac) {
        AVChannelLayout st = AV_CHANNEL_LAYOUT_STEREO;
        audio_rate = 48000; // MAIN ports reject 44100 (log_023: 0x80260008)
        if (swr_alloc_set_opts2(&swr, &st, AV_SAMPLE_FMT_S16, audio_rate, &ac->ch_layout, ac->sample_fmt,
                                ac->sample_rate, 0, NULL) < 0 || swr_init(swr) < 0)
            swr_free(&swr);
        ring_w = ring_r = 0;
        audio_run = 1;
        th = sceKernelCreateThread("movie_audio", audio_thread, 0x10000100, 0x4000, 0, 0, NULL);
        sceKernelStartThread(th, 0, NULL);
    }

    GLint prev_tex;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    GLuint tex;
    glGenTextures(1, &tex);
    int w = vc->width, h = vc->height, cw = (w + 1) / 2, ch = (h + 1) / 2;
    uint8_t *yuv = malloc(w * h + 2 * cw * ch);
    double tb = av_q2d(fmt->streams[vi]->time_base);
    AVPacket *pkt = av_packet_alloc();
    AVFrame *f = av_frame_alloc();
    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    int frames = 0, quit = 0;
    SceCtrlData pad, old;
    sceCtrlPeekBufferPositive(0, &old, 1);
    static int16_t tmp[8192 * 2];

    while (!quit && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == ai && swr) {
            avcodec_send_packet(ac, pkt);
            while (avcodec_receive_frame(ac, f) == 0) {
                uint8_t *dst = (uint8_t *)tmp;
                int n = swr_convert(swr, &dst, 8192, (const uint8_t **)f->extended_data, f->nb_samples);
                for (int i = 0; i < n * 2 && ring_w - ring_r < RING; i++)
                    ring[ring_w++ % RING] = tmp[i];
            }
        } else if (pkt->stream_index == vi) {
            avcodec_send_packet(vc, pkt);
            while (!quit && avcodec_receive_frame(vc, f) == 0) {
                // Pace on the wall clock from the start (the audio ring starts with it).
                int64_t due = (int64_t)(f->best_effort_timestamp * tb * 1e6);
                int64_t now = sceKernelGetProcessTimeWide() - t0;
                if (due > now)
                    sceKernelDelayThread(due - now);
                for (int y = 0; y < h; y++)
                    memcpy(yuv + y * w, f->data[0] + y * f->linesize[0], w);
                for (int y = 0; y < ch; y++) {
                    memcpy(yuv + w * h + y * cw, f->data[1] + y * f->linesize[1], cw);
                    memcpy(yuv + w * h + cw * ch + y * cw, f->data[2] + y * f->linesize[2], cw);
                }
                glBindTexture(GL_TEXTURE_2D, tex);
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, VGL_YUV420P_BT601, w, h, 0, w * h + 2 * cw * ch, yuv);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glBindTexture(GL_TEXTURE_2D, prev_tex);
                gles_present_movie(tex, w, h);
                if (!frames++)
                    l_info("movie: first frame shown");
                sceCtrlPeekBufferPositive(0, &pad, 1);
                if (pad.buttons & ~old.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START))
                    quit = 1;
                old = pad;
            }
        }
        av_packet_unref(pkt);
    }
    unsigned ms = (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000);
    l_info("movie: done (%d frames in %u ms%s)", frames, ms, quit ? ", skipped" : "");

    if (th >= 0) {
        if (!quit) // let the buffered tail play out
            while (ring_r != ring_w)
                sceKernelDelayThread(10000);
        audio_run = 0;
        sceKernelWaitThreadEnd(th, NULL, NULL);
    }
    swr_free(&swr);
    av_frame_free(&f);
    av_packet_free(&pkt);
    free(yuv);
    glDeleteTextures(1, &tex);
out:
    avcodec_free_context(&vc);
    avcodec_free_context(&ac);
    avformat_close_input(&fmt);
}
