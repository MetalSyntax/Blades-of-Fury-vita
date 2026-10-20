/**
 * @file patch.c
 * @brief Game-specific hooks, memory patches, and custom symbol intercepts.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <psp2/apputil.h>
#include <psp2/system_param.h>

#include "imports.h"
#include "input.h"
#include "log.h"
#include "macho.h"
#include "patch.h"

static void hook_GetDeviceLanguage(char *out_buf) {
    if (out_buf) {
        int lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
        sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
        const char *lcode = "en";
        switch (lang) {
            case SCE_SYSTEM_PARAM_LANG_FRENCH: lcode = "fr"; break;
            case SCE_SYSTEM_PARAM_LANG_SPANISH: lcode = "es"; break;
            case SCE_SYSTEM_PARAM_LANG_GERMAN: lcode = "de"; break;
            case SCE_SYSTEM_PARAM_LANG_ITALIAN: lcode = "it"; break;
            case SCE_SYSTEM_PARAM_LANG_JAPANESE: lcode = "ja"; break;
            default: lcode = "en"; break;
        }
        strcpy(out_buf, lcode);
        l_info("_GetDeviceLanguage: returning '%s' (Vita system lang: %d)", out_buf, lang);
    }
}

static int hook_SoundEngine_LoadBackgroundMusicTrack(const char *path, int loop, int crossfade) {
    l_info("_SoundEngine_LoadBackgroundMusicTrack('%s') -> stubbed (no AudioQueue)", path ? path : "null");
    return -1;
}

static int hook_SoundEngine_StartBackgroundMusic(int loop) {
    l_info("_SoundEngine_StartBackgroundMusic -> stubbed");
    return 0;
}

static int hook_SoundEngine_StopBackgroundMusic(int all) {
    return 0;
}

static int hook_SoundEngine_UnloadBackgroundMusicTrack(void) {
    return 0;
}

static int hook_SoundEngine_SetBackgroundMusicVolume(float vol) {
    return 0;
}

// VFPv2 vector mode hooks:
// Gameloft compiled Matrix4Mul, Matrix4Vector3Mul, and Matrix4Vector3MulAndRot with FPSCR.LEN=3
// (vector mode length 4), which is deprecated and causes an Undefined Instruction exception (0x30002)
// on Cortex-A9 MPCore (PS Vita). Replacing them with standard scalar matrix math fixes the crash.

static void hook_Matrix4Mul(const float *a, const float *b, float *out) {
    float tmp[16];
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            tmp[col * 4 + row] = a[row] * b[col * 4 + 0] +
                                 a[4 + row] * b[col * 4 + 1] +
                                 a[8 + row] * b[col * 4 + 2] +
                                 a[12 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, tmp, sizeof(tmp));
}

static void hook_Matrix4Vector3Mul(const float *m, const float *v, float *out) {
    float tmp[4];
    for (int row = 0; row < 4; row++) {
        tmp[row] = m[row] * v[0] +
                   m[4 + row] * v[1] +
                   m[8 + row] * v[2] +
                   m[12 + row];
    }
    memcpy(out, tmp, sizeof(tmp));
}

static void hook_Matrix4Vector3MulAndRot(const float *m, const float *v1, float *out1,
                                         const float *v2, float *out2, float scale) {
    float tmp1[4];
    float tmp2[3];
    for (int row = 0; row < 4; row++) {
        tmp1[row] = (m[row] * v1[0] +
                     m[4 + row] * v1[1] +
                     m[8 + row] * v1[2] +
                     m[12 + row]) * scale;
    }
    for (int row = 0; row < 3; row++) {
        tmp2[row] = (m[row] * v2[0] +
                     m[4 + row] * v2[1] +
                     m[8 + row] * v2[2]) * scale;
    }
    memcpy(out1, tmp1, sizeof(tmp1));
    memcpy(out2, tmp2, sizeof(tmp2));
}

// Game-specific import tables for the port being developed.
// Register any guest binary symbols you wish to intercept or replace here.
const import_entry patch_imports[] = {
    // Example:
    // F(my_custom_function, my_implementation),
};
const int patch_imports_count = sizeof(patch_imports) / sizeof(patch_imports[0]);

void patch_init(void) {
    l_info("Initializing game-specific patches...");
    macho_hook("_GetDeviceLanguage", hook_GetDeviceLanguage);
    macho_hook("_SoundEngine_LoadBackgroundMusicTrack", hook_SoundEngine_LoadBackgroundMusicTrack);
    macho_hook("_SoundEngine_StartBackgroundMusic", hook_SoundEngine_StartBackgroundMusic);
    macho_hook("_SoundEngine_StopBackgroundMusic", hook_SoundEngine_StopBackgroundMusic);
    macho_hook("_SoundEngine_UnloadBackgroundMusicTrack", hook_SoundEngine_UnloadBackgroundMusicTrack);
    macho_hook("_SoundEngine_SetBackgroundMusicVolume", hook_SoundEngine_SetBackgroundMusicVolume);

    // VFP vector mode replacement hooks
    macho_hook("__Z10Matrix4MulPKfS0_Pf", hook_Matrix4Mul);
    macho_hook("__Z17Matrix4Vector3MulPKfS0_Pf", hook_Matrix4Vector3Mul);
    macho_hook("__Z23Matrix4Vector3MulAndRotPKfS0_PfS0_S1_f", hook_Matrix4Vector3MulAndRot);
}

void patch_frame(void) {
    // Per-frame logic if required by target engine
}

static void add_synthetic_touch(int id, int gx, int gy) {
    if (touch_count >= MAX_TOUCHES)
        return;
    touches[touch_count].id = id;
    touches[touch_count].x = gx * 960 / 480;
    touches[touch_count].y = gy * 544 / 320;
    touches[touch_count].gx = gx;
    touches[touch_count].gy = gy;
    touches[touch_count].down = 1;
    touch_count++;
}

void patch_input(void) {
    // 1. Left stick / D-Pad -> Virtual D-Pad / Joystick at (80, 250)
    float stick_dx = 0.0f;
    float stick_dy = 0.0f;

    // Analog stick (0..255, center 128)
    float ax = (pad.lx - 128.0f) / 128.0f;
    float ay = (pad.ly - 128.0f) / 128.0f;
    if (ax * ax + ay * ay > 0.06f) { // deadzone ~0.25
        stick_dx = ax;
        stick_dy = ay;
    }

    // D-Pad buttons
    if (pad.buttons & SCE_CTRL_UP)    stick_dy -= 1.0f;
    if (pad.buttons & SCE_CTRL_DOWN)  stick_dy += 1.0f;
    if (pad.buttons & SCE_CTRL_LEFT)  stick_dx -= 1.0f;
    if (pad.buttons & SCE_CTRL_RIGHT) stick_dx += 1.0f;

    float mag = sqrtf(stick_dx * stick_dx + stick_dy * stick_dy);
    if (mag > 0.15f) {
        if (mag > 1.0f) {
            stick_dx /= mag;
            stick_dy /= mag;
        }
        int joy_x = 80 + (int)(stick_dx * 60.0f);
        int joy_y = 250 + (int)(stick_dy * 60.0f);
        if (joy_x < 0) joy_x = 0;
        if (joy_x > 479) joy_x = 479;
        if (joy_y < 0) joy_y = 0;
        if (joy_y > 319) joy_y = 319;
        add_synthetic_touch(0x10, joy_x, joy_y);
    }

    // 2. Action buttons (Blades of Fury HUD positions)
    // Square: Attack A (Horizontal Slash, 361, 255)
    if (pad.buttons & SCE_CTRL_SQUARE) {
        add_synthetic_touch(0x11, 361, 255);
    }

    // Triangle: Attack B (Vertical Slash, 410, 206)
    if (pad.buttons & SCE_CTRL_TRIANGLE) {
        add_synthetic_touch(0x12, 410, 206);
    }

    // Circle: Attack C (Kick / Magic, 430, 157) or R1
    if ((pad.buttons & SCE_CTRL_CIRCLE) || (pad.buttons & SCE_CTRL_R1)) {
        add_synthetic_touch(0x13, 430, 157);
    }

    // Cross: Attack D (Guard / Defense, 430, 270) or L1
    if ((pad.buttons & SCE_CTRL_CROSS) || (pad.buttons & SCE_CTRL_L1)) {
        add_synthetic_touch(0x14, 430, 270);
    }

    // Start: Pause button (top-right, 455, 25)
    if (pad.buttons & SCE_CTRL_START) {
        add_synthetic_touch(0x15, 455, 25);
    }
}

void patch_overlay(void) {
    // Loader OSD on the 960x544 screen (scissor + clear only) goes here
}
