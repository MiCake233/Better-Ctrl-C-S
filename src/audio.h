#ifndef AUDIO_H
#define AUDIO_H

#include "app.h"

typedef struct {
    BYTE* wav;
    DWORD size;
} AudioClip;

BOOL AudioInit(void);
void AudioShutdown(void);

const AudioClip* AudioBeep(void);
const AudioClip* AudioCustom(void);

BOOL AudioLoadWav(const wchar_t* path, AudioClip* out, wchar_t* err,
                  int cchErr);
BOOL AudioSaveWav(const AudioClip* clip, const wchar_t* path);
void AudioFree(AudioClip* clip);
BOOL AudioPlay(const AudioClip* clip, int volume);
BOOL AudioReloadCustom(void);

#endif /* AUDIO_H */
