#include "audio.h"
#include "util.h"

#include <math.h>
#include <mmsystem.h>
#include <string.h>

#define BEEP_RATE 22050
#define BEEP_MS   90
#define MAX_WAV_BYTES (16u * 1024u * 1024u)
#define MAX_SECONDS   10u
#define MAX_SAMPLES   4000000u

static AudioClip g_beep;
static AudioClip g_custom;
static BYTE* g_mix[2];
static DWORD g_mixCap[2];
static int g_mixIndex;
static BOOL g_soundFailedLogged;

static WORD Rd16(const BYTE* p)
{
    return (WORD)(p[0] | ((WORD)p[1] << 8));
}

static DWORD Rd32(const BYTE* p)
{
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) |
           ((DWORD)p[3] << 24);
}

static void Put16(BYTE* p, WORD v)
{
    p[0] = (BYTE)(v & 0xFF);
    p[1] = (BYTE)((v >> 8) & 0xFF);
}

static void Put32(BYTE* p, DWORD v)
{
    p[0] = (BYTE)(v & 0xFF);
    p[1] = (BYTE)((v >> 8) & 0xFF);
    p[2] = (BYTE)((v >> 16) & 0xFF);
    p[3] = (BYTE)((v >> 24) & 0xFF);
}

static void WriteWavHeader(BYTE* h, DWORD dataSize, WORD channels, DWORD rate)
{
    memcpy(h, "RIFF", 4);
    Put32(h + 4, 36 + dataSize);
    memcpy(h + 8, "WAVEfmt ", 8);
    Put32(h + 16, 16);
    Put16(h + 20, 1); /* PCM */
    Put16(h + 22, channels);
    Put32(h + 24, rate);
    Put32(h + 28, rate * channels * 2);
    Put16(h + 32, (WORD)(channels * 2));
    Put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    Put32(h + 40, dataSize);
}

void AudioFree(AudioClip* clip)
{
    if (!clip)
        return;
    if (clip->wav)
        HeapFree(GetProcessHeap(), 0, clip->wav);
    clip->wav = NULL;
    clip->size = 0;
}

static BOOL BuildBeep(AudioClip* clip)
{
    const DWORD frames = BEEP_RATE * BEEP_MS / 1000;
    const DWORD dataSize = frames * 2;
    const double twoPi = 6.28318530717958647692;
    BYTE* wav;
    short* samples;
    DWORD i;

    wav = (BYTE*)HeapAlloc(GetProcessHeap(), 0, 44 + dataSize);
    if (!wav)
        return FALSE;

    WriteWavHeader(wav, dataSize, 1, BEEP_RATE);
    samples = (short*)(wav + 44);

    for (i = 0; i < frames; i++) {
        double t = (double)i / BEEP_RATE;
        double env = 1.0;
        double tail = (double)(frames - i) / BEEP_RATE;
        double v;

        if (t < 0.004)
            env = t / 0.004;
        if (tail < 0.030) {
            double e2 = tail / 0.030;
            if (e2 < env)
                env = e2;
        }
        v = sin(twoPi * 880.0 * t) * 0.45 * env;
        samples[i] = (short)(v * 32767.0);
    }

    clip->wav = wav;
    clip->size = 44 + dataSize;
    return TRUE;
}

static void SetError(wchar_t* err, int cchErr, const wchar_t* text)
{
    if (err && cchErr > 0) {
        wcsncpy(err, text, (size_t)cchErr - 1);
        err[cchErr - 1] = 0;
    }
}

BOOL AudioLoadWav(const wchar_t* path, AudioClip* out, wchar_t* err, int cchErr)
{
    BYTE* raw = NULL;
    DWORD size = 0;
    DWORD off;
    const BYTE* fmt = NULL;
    DWORD fmtSize = 0;
    const BYTE* data = NULL;
    DWORD dataSize = 0;
    WORD tag;
    WORD channels;
    DWORD rate;
    WORD blockAlign;
    WORD bits;
    DWORD frameBytes;
    DWORD frames;
    DWORD samples;
    BYTE* wav;
    short* dst;
    DWORD f;
    DWORD ch;

    if (err && cchErr > 0)
        err[0] = 0;
    if (!out)
        return FALSE;
    out->wav = NULL;
    out->size = 0;

    if (!UtilReadFile(path, MAX_WAV_BYTES, &raw, &size)) {
        SetError(err, cchErr, L"Could not read the file.");
        return FALSE;
    }

    if (size < 44 || memcmp(raw, "RIFF", 4) != 0 ||
        memcmp(raw + 8, "WAVE", 4) != 0) {
        SetError(err, cchErr, L"Not a valid WAV file.");
        goto fail;
    }

    off = 12;
    while (off + 8 <= size) {
        DWORD chunkSize = Rd32(raw + off + 4);
        const BYTE* body = raw + off + 8;
        if (chunkSize > size - off - 8)
            break;
        if (memcmp(raw + off, "fmt ", 4) == 0) {
            fmt = body;
            fmtSize = chunkSize;
        } else if (memcmp(raw + off, "data", 4) == 0) {
            data = body;
            dataSize = chunkSize;
        }
        off += 8 + chunkSize + (chunkSize & 1u);
    }

    if (!fmt || !data || fmtSize < 16 || dataSize == 0) {
        SetError(err, cchErr, L"Incomplete WAV file.");
        goto fail;
    }

    tag = Rd16(fmt);
    channels = Rd16(fmt + 2);
    rate = Rd32(fmt + 4);
    blockAlign = Rd16(fmt + 12);
    bits = Rd16(fmt + 14);

    if (tag == 0xFFFE) { /* WAVE_FORMAT_EXTENSIBLE */
        static const BYTE pcmGuid[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0x10, 0x00, 0x80, 0x00, 0x00, 0xAA,
                                         0x00, 0x38, 0x9B, 0x71};
        WORD cbSize;
        if (fmtSize < 40) {
            SetError(err, cchErr, L"Invalid WAV format block.");
            goto fail;
        }
        cbSize = Rd16(fmt + 16);
        if (cbSize < 22 || fmtSize < (DWORD)cbSize + 18 ||
            memcmp(fmt + 24, pcmGuid, 16) != 0) {
            SetError(err, cchErr,
                     L"Only uncompressed PCM WAV files are supported.");
            goto fail;
        }
        tag = 1;
    }

    if (tag != 1) {
        SetError(err, cchErr,
                 L"Only uncompressed PCM WAV files are supported.");
        goto fail;
    }
    if (channels < 1 || channels > 2) {
        SetError(err, cchErr, L"Only mono or stereo WAV files are supported.");
        goto fail;
    }
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) {
        SetError(err, cchErr, L"Unsupported bit depth (use 8, 16, 24 or 32).");
        goto fail;
    }
    if (rate < 1000 || rate > 384000) {
        SetError(err, cchErr, L"Unsupported sample rate.");
        goto fail;
    }

    frameBytes = (DWORD)channels * (bits / 8u);
    if (blockAlign < frameBytes || dataSize % blockAlign != 0) {
        SetError(err, cchErr, L"Invalid or truncated audio data.");
        goto fail;
    }

    frames = dataSize / blockAlign;
    if (frames == 0) {
        SetError(err, cchErr, L"The audio stream is empty.");
        goto fail;
    }
    if (frames > rate * MAX_SECONDS) {
        SetError(err, cchErr, L"The WAV file is longer than 10 seconds.");
        goto fail;
    }
    if ((unsigned long long)frames * channels > MAX_SAMPLES) {
        SetError(err, cchErr, L"The WAV file is too large.");
        goto fail;
    }

    samples = frames * channels;
    wav = (BYTE*)HeapAlloc(GetProcessHeap(), 0, 44 + (SIZE_T)samples * 2);
    if (!wav) {
        SetError(err, cchErr, L"Not enough memory to load the sound.");
        goto fail;
    }

    WriteWavHeader(wav, samples * 2, channels, rate);
    dst = (short*)(wav + 44);

    for (f = 0; f < frames; f++) {
        const BYTE* base = data + (SIZE_T)f * blockAlign;
        for (ch = 0; ch < channels; ch++) {
            const BYTE* s = base + (SIZE_T)ch * (bits / 8u);
            int v;
            if (bits == 8) {
                v = ((int)s[0] - 128) << 8;
            } else if (bits == 16) {
                v = (int)(short)(s[0] | ((WORD)s[1] << 8));
            } else if (bits == 24) {
                int t = (int)s[0] | ((int)s[1] << 8) | ((int)s[2] << 16);
                if (t & 0x800000)
                    t -= 0x1000000;
                v = t >> 8;
            } else {
                v = ((int)Rd32(s)) >> 16;
            }
            *dst++ = (short)v;
        }
    }

    HeapFree(GetProcessHeap(), 0, raw);
    out->wav = wav;
    out->size = 44 + samples * 2;
    return TRUE;

fail:
    HeapFree(GetProcessHeap(), 0, raw);
    return FALSE;
}

BOOL AudioSaveWav(const AudioClip* clip, const wchar_t* path)
{
    if (!clip || !clip->wav || clip->size < 44)
        return FALSE;
    return UtilWriteFileAtomic(path, clip->wav, clip->size);
}

static BOOL EnsureMixBuffer(int index, DWORD bytes)
{
    if (g_mixCap[index] >= bytes && g_mix[index])
        return TRUE;
    if (g_mix[index])
        HeapFree(GetProcessHeap(), 0, g_mix[index]);
    g_mix[index] = (BYTE*)HeapAlloc(GetProcessHeap(), 0, bytes);
    g_mixCap[index] = g_mix[index] ? bytes : 0;
    return g_mix[index] != NULL;
}

BOOL AudioPlay(const AudioClip* clip, int volume)
{
    DWORD samples;
    DWORD i;
    int index;
    const short* src;
    short* dst;

    PlaySoundW(NULL, NULL, 0);

    if (!clip || !clip->wav || clip->size < 44)
        return FALSE;
    if (volume <= 0)
        return TRUE;
    if (volume > 100)
        volume = 100;

    index = g_mixIndex;
    if (!EnsureMixBuffer(index, clip->size))
        return FALSE;
    g_mixIndex ^= 1;

    memcpy(g_mix[index], clip->wav, 44);
    samples = (clip->size - 44) / 2;
    src = (const short*)(clip->wav + 44);
    dst = (short*)(g_mix[index] + 44);

    for (i = 0; i < samples; i++) {
        int v = (int)src[i] * volume / 100;
        if (v > 32767)
            v = 32767;
        if (v < -32768)
            v = -32768;
        dst[i] = (short)v;
    }

    if (!PlaySoundW((LPCWSTR)g_mix[index], NULL,
                    SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) {
        if (!g_soundFailedLogged) {
            UtilLog(L"AudioPlay: PlaySound failed (%lu)", GetLastError());
            g_soundFailedLogged = TRUE;
        }
        return FALSE;
    }
    return TRUE;
}

BOOL AudioReloadCustom(void)
{
    wchar_t err[256];

    AudioFree(&g_custom);
    if (!g_config.custom_sound)
        return TRUE;

    if (!UtilFileExists(g_customWavPath)) {
        UtilLog(L"AudioReloadCustom: %s not found", g_customWavPath);
        return FALSE;
    }
    if (!AudioLoadWav(g_customWavPath, &g_custom, err, 256)) {
        UtilLog(L"AudioReloadCustom: %s", err);
        return FALSE;
    }
    return TRUE;
}

const AudioClip* AudioBeep(void)
{
    return g_beep.wav ? &g_beep : NULL;
}

const AudioClip* AudioCustom(void)
{
    return g_custom.wav ? &g_custom : NULL;
}

BOOL AudioInit(void)
{
    if (!BuildBeep(&g_beep)) {
        UtilLog(L"AudioInit: cannot allocate the built-in tone");
        return FALSE;
    }
    AudioReloadCustom();
    return TRUE;
}

void AudioShutdown(void)
{
    PlaySoundW(NULL, NULL, 0);
    AudioFree(&g_beep);
    AudioFree(&g_custom);
    if (g_mix[0]) {
        HeapFree(GetProcessHeap(), 0, g_mix[0]);
        g_mix[0] = NULL;
        g_mixCap[0] = 0;
    }
    if (g_mix[1]) {
        HeapFree(GetProcessHeap(), 0, g_mix[1]);
        g_mix[1] = NULL;
        g_mixCap[1] = 0;
    }
}
