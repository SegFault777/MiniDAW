/* MiniDAW plug-in API, version 1.
 *
 * A MiniDAW plug-in is a shared library (.dll / .so / .dylib) in the "plugins" folder next to MiniDAW.exe
 * or in %LOCALAPPDATA%/MiniDAW/plugins. It exports two C functions:
 *
 *     int                  mdplugin_count(void);
 *     const MdEffectDesc*  mdplugin_get(int index);
 *
 * and each MdEffectDesc describes one stereo audio effect. The descriptors must stay valid for as long as the
 * library is loaded (use static storage). Audio is interleaved stereo float at the sample rate passed to
 * create(). All callbacks except process() run on the user interface thread; process() runs on the audio
 * thread, so it must not allocate memory, lock, or block.
 *
 * This header is plain C and can be copied into your own project. MiniDAW is GPL-3.0-or-later; a plug-in that
 * only uses this interface is a separate work and can use any license.
 */
#ifndef MINIDAW_MDPLUGIN_H
#define MINIDAW_MDPLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MDPLUGIN_API_VERSION 1
#define MDPLUGIN_MAX_PARAMS 32

typedef struct MdParam {
    const char* name;   /* shown on the knob, for example "Rate" */
    const char* unit;   /* suffix such as "Hz" or "dB"; may be NULL */
    float min, max, def;
} MdParam;

typedef struct MdEffectDesc {
    uint32_t apiVersion;      /* MDPLUGIN_API_VERSION */
    const char* id;           /* unique and stable, for example "com.example.tremolo" (saved in projects) */
    const char* name;         /* shown in the effect menu */
    const char* vendor;       /* may be NULL */
    int paramCount;           /* 0 .. MDPLUGIN_MAX_PARAMS */
    const MdParam* params;
    void* (*create)(int sampleRate);                     /* returns a new instance, NULL on failure */
    void (*destroy)(void* instance);
    void (*reset)(void* instance);                       /* clear delay lines and filter state */
    void (*setParam)(void* instance, int index, float value);
    void (*process)(void* instance, float* stereo, int frames);   /* in place, interleaved L R L R ... */
} MdEffectDesc;

#if defined(_WIN32)
#define MDPLUGIN_EXPORT __declspec(dllexport)
#else
#define MDPLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
}
#endif

#endif /* MINIDAW_MDPLUGIN_H */
