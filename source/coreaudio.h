/**
 * @file coreaudio.h
 * @brief CoreAudio, AudioFile, and BGM subsystem interfaces for PS Vita.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int sound_bgm_load(const char *path, int loop, int crossfade);
int sound_bgm_start(int loop);
int sound_bgm_stop(int all);
int sound_bgm_unload(void);
int sound_bgm_set_volume(float vol);

#ifdef __cplusplus
}
#endif
