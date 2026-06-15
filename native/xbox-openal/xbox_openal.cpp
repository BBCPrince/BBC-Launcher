// xbox_openal.cpp
//
// Stub replacement for OpenAL.dll on Xbox UWP.
//
// Why this exists:
//   LWJGL ships OpenAL Soft as OpenAL.dll alongside lwjgl-openal natives.  The
//   real OpenAL Soft on Windows imports from dsound.dll (DirectSound), winmm,
//   mmdevapi, ole32, etc.  Xbox UWP does not ship dsound.dll, so LoadLibrary
//   on the real OpenAL.dll fails with:
//
//     UnsatisfiedLinkError: Failed to load library: ...\OpenAL.dll
//                           (error code = 127  -> ERROR_PROC_NOT_FOUND)
//
//   ALC.<clinit> calls Library.loadNative("OpenAL") on render-thread init of
//   the SoundEngine, and the JVM is killed before the main menu paints.
//
//   We point LWJGL at this stub via -Dorg.lwjgl.openal.libname=<absolute>.
//   Older builds used a no-audio stub where alcOpenDevice returned NULL.  This
//   file now keeps the same OpenAL-facing API but backs simple buffer playback
//   with XAudio2 so Minecraft can get menu/game sound on Xbox UWP.
//
// Goals (milestone 1, same pattern as xbox-glfw / xbox-opengl):
//   * Export every AL/ALC symbol LWJGL queries via GetProcAddress on the DLL
//     handle so Library.create() doesn't fail looking up "alcOpenDevice".
//   * alc* returns a real-enough device/context for Mojang's SoundEngine.
//   * alBufferData stores PCM assets; alSourcePlay submits them to XAudio2.
//   * alGetError / alcGetError report no error so init paths don't trip on
//     the "did we error during probe?" check.
//   * EFX / SOFT extensions exported in xbox_openal.def alias to
//     universal_no_op for any code that GetProcAddress's them directly off
//     the DLL handle instead of via alGetProcAddress.

#include <windows.h>
#include <xaudio2.h>
#include <objbase.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

extern "C" {

typedef int             ALboolean;
typedef int             ALCboolean;
typedef int             ALenum;
typedef int             ALCenum;
typedef int             ALsizei;
typedef int             ALCsizei;
typedef int             ALint;
typedef int             ALCint;
typedef unsigned int    ALuint;
typedef unsigned int    ALCuint;
typedef float           ALfloat;
typedef double          ALdouble;
typedef char            ALchar;
typedef char            ALCchar;
typedef void            ALvoid;
typedef void            ALCvoid;

#define ALC_FALSE          0
#define ALC_TRUE           1
#define ALC_NO_ERROR       0
#define ALC_INVALID_DEVICE 0xA001
#define ALC_INVALID_VALUE  0xA004

#define AL_FALSE           0
#define AL_TRUE            1
#define AL_NO_ERROR        0

#define ALC_DEFAULT_DEVICE_SPECIFIER         0x1004
#define ALC_DEVICE_SPECIFIER                 0x1005
#define ALC_EXTENSIONS                       0x1006
#define ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER 0x311
#define ALC_CAPTURE_DEVICE_SPECIFIER         0x310
#define ALC_DEFAULT_ALL_DEVICES_SPECIFIER    0x1012
#define ALC_ALL_DEVICES_SPECIFIER            0x1013
#define ALC_FREQUENCY                        0x1007
#define ALC_REFRESH                          0x1008
#define ALC_SYNC                             0x1009
#define ALC_MONO_SOURCES                     0x1010
#define ALC_STEREO_SOURCES                   0x1011
#define ALC_MAJOR_VERSION                    0x1000
#define ALC_MINOR_VERSION                    0x1001
#define ALC_ATTRIBUTES_SIZE                  0x1002
#define ALC_ALL_ATTRIBUTES                   0x1003

#define AL_INVALID_NAME  0xA001
#define AL_INVALID_ENUM  0xA002
#define AL_INVALID_VALUE 0xA003
#define AL_INVALID_OPERATION 0xA004
#define AL_OUT_OF_MEMORY 0xA005

#define AL_VENDOR     0xB001
#define AL_VERSION    0xB002
#define AL_RENDERER   0xB003
#define AL_EXTENSIONS 0xB004

#define AL_PITCH              0x1003
#define AL_POSITION           0x1004
#define AL_VELOCITY           0x1006
#define AL_LOOPING            0x1007
#define AL_BUFFER             0x1009
#define AL_GAIN               0x100A
#define AL_ORIENTATION        0x100F
#define AL_SOURCE_STATE       0x1010
#define AL_INITIAL            0x1011
#define AL_PLAYING            0x1012
#define AL_PAUSED             0x1013
#define AL_STOPPED            0x1014
#define AL_BUFFERS_QUEUED     0x1015
#define AL_BUFFERS_PROCESSED  0x1016
#define AL_SOURCE_RELATIVE    0x202
#define AL_SOURCE_DISTANCE_MODEL 0x200
#define AL_ROLLOFF_FACTOR     0x1021
#define AL_REFERENCE_DISTANCE 0x1020
#define AL_MAX_DISTANCE       0x1023

#define AL_NONE                      0
#define AL_DISTANCE_MODEL            0xD000
#define AL_INVERSE_DISTANCE          0xD001
#define AL_INVERSE_DISTANCE_CLAMPED  0xD002
#define AL_LINEAR_DISTANCE           0xD003
#define AL_LINEAR_DISTANCE_CLAMPED   0xD004
#define AL_EXPONENT_DISTANCE         0xD005
#define AL_EXPONENT_DISTANCE_CLAMPED 0xD006

#define AL_FORMAT_MONO8    0x1100
#define AL_FORMAT_MONO16   0x1101
#define AL_FORMAT_STEREO8  0x1102
#define AL_FORMAT_STEREO16 0x1103

#define AL_FREQUENCY 0x2001
#define AL_BITS      0x2002
#define AL_CHANNELS  0x2003
#define AL_SIZE      0x2004

// -----------------------------------------------------------------------------
// Universal no-op for EFX / SOFT exports listed in xbox_openal.def.
// -----------------------------------------------------------------------------

__declspec(noinline) int64_t universal_no_op() {
    return 0;
}

// Strings need static storage so the returned pointer stays valid.
// Device specifier queries use a double-null terminator to denote an
// empty list (OpenAL convention).
static const char kEmptyList[] = { '\0', '\0' };
static const char kDeviceList[] = { 'X','b','o','x',' ','X','A','u','d','i','o','2','\0','\0' };
static const char kAlVendor[]     = "Xbox";
static const char kAlRenderer[]   = "xbox-openal XAudio2 bridge";
static const char kAlVersion[]    = "1.1 Xbox-XAudio2";
static const char kAlExtensions[] =
    "AL_EXT_source_distance_model "
    "AL_EXT_LINEAR_DISTANCE "
    "AL_EXT_EXPONENT_DISTANCE";
static const char kAlcExtensions[] = "";

static INIT_ONCE g_audioLockInit = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_audioLock;
static IXAudio2* g_xaudio = nullptr;
static IXAudio2MasteringVoice* g_masterVoice = nullptr;
static bool g_audioInitAttempted = false;
static bool g_audioReady = false;
static void* g_currentContext = nullptr;
static uint8_t g_deviceSentinel = 1;
static uint8_t g_contextSentinel = 1;

static constexpr int kMaxAudioBuffers = 4096;
static constexpr int kMaxAudioSources = 256;
static constexpr int kMaxQueuedBuffers = 64;

struct AudioBufferRecord {
    ALuint id;
    ALenum format;
    ALsizei frequency;
    ALsizei bytes;
    ALsizei channels;
    ALsizei bits;
    uint8_t* data;
    WAVEFORMATEX wave;
};

struct AudioVec3 {
    ALfloat x;
    ALfloat y;
    ALfloat z;
};

struct AudioSourceRecord {
    ALuint id;
    ALuint buffer;
    ALuint queued[kMaxQueuedBuffers];
    ALsizei queuedCount;
    ALfloat gain;
    ALfloat pitch;
    ALfloat rolloff;
    ALfloat referenceDistance;
    ALfloat maxDistance;
    AudioVec3 position;
    AudioVec3 velocity;
    ALboolean relative;
    ALboolean looping;
    ALenum distanceModel;
    ALenum state;
    IXAudio2SourceVoice* voice;
    ALsizei voiceChannels;
    ALsizei submittedCount;
    WAVEFORMATEX voiceWave;
    bool voiceWaveValid;
};

static AudioBufferRecord g_buffers[kMaxAudioBuffers] = {};
static AudioSourceRecord g_sources[kMaxAudioSources] = {};
static ALuint g_nextSourceId = 1;
static ALuint g_nextBufferId = 1;
static LONG g_bufferDataLogCount = 0;
static LONG g_sourcePlayLogCount = 0;
static LONG g_queuedPlayLogCount = 0;
static AudioVec3 g_listenerPosition = { 0.0f, 0.0f, 0.0f };
static AudioVec3 g_listenerVelocity = { 0.0f, 0.0f, 0.0f };
static AudioVec3 g_listenerAt = { 0.0f, 0.0f, -1.0f };
static AudioVec3 g_listenerUp = { 0.0f, 1.0f, 0.0f };
static ALfloat g_listenerGain = 1.0f;
static ALenum g_distanceModel = AL_INVERSE_DISTANCE_CLAMPED;
static ALsizei g_masterChannels = 2;

static BOOL CALLBACK InitAudioLock(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_audioLock);
    return TRUE;
}

static void LockAudio() {
    InitOnceExecuteOnce(&g_audioLockInit, InitAudioLock, nullptr, nullptr);
    EnterCriticalSection(&g_audioLock);
}

static void UnlockAudio() {
    LeaveCriticalSection(&g_audioLock);
}

static void AudioLog(const char* message) {
    if (!message) {
        return;
    }

    char debugLine[384] = {};
    std::snprintf(debugLine, sizeof(debugLine), "[Native][xbox-openal] %s\r\n", message);
    OutputDebugStringA(debugLine);

    wchar_t logPath[512] = {};
    DWORD len = GetEnvironmentVariableW(
        L"MINECRAFT_XBOX_NATIVE_LOG",
        logPath,
        static_cast<DWORD>(sizeof(logPath) / sizeof(logPath[0])));
    if (len == 0 || len >= sizeof(logPath) / sizeof(logPath[0])) {
        return;
    }

    HANDLE file = CreateFileW(
        logPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, debugLine, static_cast<DWORD>(std::strlen(debugLine)), &written, nullptr);
    CloseHandle(file);
}

static bool EnsureAudioEngineLocked() {
    if (g_audioReady) {
        return true;
    }
    if (g_audioInitAttempted) {
        return false;
    }

    g_audioInitAttempted = true;
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)co;

    HRESULT hr = XAudio2Create(&g_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr) || !g_xaudio) {
        char line[160] = {};
        std::snprintf(line, sizeof(line), "XAudio2Create failed hr=0x%08X", static_cast<unsigned>(hr));
        AudioLog(line);
        g_xaudio = nullptr;
        return false;
    }

    hr = g_xaudio->CreateMasteringVoice(&g_masterVoice);
    if (FAILED(hr) || !g_masterVoice) {
        char line[192] = {};
        std::snprintf(line, sizeof(line), "CreateMasteringVoice failed hr=0x%08X", static_cast<unsigned>(hr));
        AudioLog(line);
        g_xaudio->Release();
        g_xaudio = nullptr;
        g_masterVoice = nullptr;
        return false;
    }

    XAUDIO2_VOICE_DETAILS details = {};
    g_masterVoice->GetVoiceDetails(&details);
    if (details.InputChannels > 0) {
        g_masterChannels = static_cast<ALsizei>(details.InputChannels);
    }

    g_audioReady = true;
    AudioLog("XAudio2 mastering voice ready");
    return true;
}

static bool FormatToWave(ALenum format, ALsizei frequency, WAVEFORMATEX& wave, ALsizei& channels, ALsizei& bits) {
    channels = 0;
    bits = 0;
    switch (format) {
        case AL_FORMAT_MONO8:    channels = 1; bits = 8;  break;
        case AL_FORMAT_MONO16:   channels = 1; bits = 16; break;
        case AL_FORMAT_STEREO8:  channels = 2; bits = 8;  break;
        case AL_FORMAT_STEREO16: channels = 2; bits = 16; break;
        default: return false;
    }

    if (frequency <= 0) {
        return false;
    }

    std::memset(&wave, 0, sizeof(wave));
    wave.wFormatTag = WAVE_FORMAT_PCM;
    wave.nChannels = static_cast<WORD>(channels);
    wave.nSamplesPerSec = static_cast<DWORD>(frequency);
    wave.wBitsPerSample = static_cast<WORD>(bits);
    wave.nBlockAlign = static_cast<WORD>((channels * bits) / 8);
    wave.nAvgBytesPerSec = wave.nSamplesPerSec * wave.nBlockAlign;
    return true;
}

static float ClampFloat(float value, float minValue, float maxValue);

static bool SameWaveFormat(const WAVEFORMATEX& a, const WAVEFORMATEX& b) {
    return a.wFormatTag == b.wFormatTag &&
        a.nChannels == b.nChannels &&
        a.nSamplesPerSec == b.nSamplesPerSec &&
        a.wBitsPerSample == b.wBitsPerSample &&
        a.nBlockAlign == b.nBlockAlign;
}

static bool IsDistanceModel(ALenum model) {
    return model == AL_NONE ||
        model == AL_INVERSE_DISTANCE ||
        model == AL_INVERSE_DISTANCE_CLAMPED ||
        model == AL_LINEAR_DISTANCE ||
        model == AL_LINEAR_DISTANCE_CLAMPED ||
        model == AL_EXPONENT_DISTANCE ||
        model == AL_EXPONENT_DISTANCE_CLAMPED;
}

static float DistanceBetween(const AudioVec3& a, const AudioVec3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

static AudioVec3 SubtractVec3(const AudioVec3& a, const AudioVec3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

static float DotVec3(const AudioVec3& a, const AudioVec3& b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

static AudioVec3 CrossVec3(const AudioVec3& a, const AudioVec3& b) {
    return {
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x)
    };
}

static AudioVec3 NormalizeVec3(const AudioVec3& value, const AudioVec3& fallback) {
    const float length = std::sqrt(DotVec3(value, value));
    if (length <= 0.0001f) {
        return fallback;
    }
    return { value.x / length, value.y / length, value.z / length };
}

static float ComputeDistanceAttenuation(const AudioSourceRecord* source) {
    if (!source) {
        return 0.0f;
    }

    const ALenum model = source->distanceModel != AL_NONE ? source->distanceModel : g_distanceModel;
    if (model == AL_NONE) {
        return 1.0f;
    }

    const float reference = source->referenceDistance > 0.0f ? source->referenceDistance : 1.0f;
    const float maxDistance = source->maxDistance > reference ? source->maxDistance : reference;
    const float rolloff = source->rolloff < 0.0f ? 0.0f : source->rolloff;
    const AudioVec3 origin = { 0.0f, 0.0f, 0.0f };
    float distance = source->relative
        ? DistanceBetween(source->position, origin)
        : DistanceBetween(source->position, g_listenerPosition);

    const bool clamped =
        model == AL_INVERSE_DISTANCE_CLAMPED ||
        model == AL_LINEAR_DISTANCE_CLAMPED ||
        model == AL_EXPONENT_DISTANCE_CLAMPED;
    if (clamped) {
        distance = ClampFloat(distance, reference, maxDistance);
    }

    float attenuation = 1.0f;
    if (model == AL_INVERSE_DISTANCE || model == AL_INVERSE_DISTANCE_CLAMPED) {
        const float denom = reference + (rolloff * (distance - reference));
        attenuation = denom > 0.0f ? reference / denom : 1.0f;
    } else if (model == AL_LINEAR_DISTANCE || model == AL_LINEAR_DISTANCE_CLAMPED) {
        const float span = maxDistance - reference;
        attenuation = span > 0.0f ? 1.0f - (rolloff * (distance - reference) / span) : 1.0f;
    } else if (model == AL_EXPONENT_DISTANCE || model == AL_EXPONENT_DISTANCE_CLAMPED) {
        attenuation = distance > 0.0f ? std::pow(distance / reference, -rolloff) : 1.0f;
    }

    return ClampFloat(attenuation, 0.0f, 1.0f);
}

static float ComputeSourcePanLocked(const AudioSourceRecord* source) {
    if (!source) {
        return 0.0f;
    }

    AudioVec3 direction = {};
    if (source->relative) {
        direction = source->position;
    } else {
        direction = SubtractVec3(source->position, g_listenerPosition);
    }

    const AudioVec3 forward = NormalizeVec3(g_listenerAt, { 0.0f, 0.0f, -1.0f });
    const AudioVec3 up = NormalizeVec3(g_listenerUp, { 0.0f, 1.0f, 0.0f });
    const AudioVec3 right = NormalizeVec3(CrossVec3(forward, up), { 1.0f, 0.0f, 0.0f });
    const AudioVec3 normalizedDirection = NormalizeVec3(direction, { 0.0f, 0.0f, -1.0f });
    return ClampFloat(DotVec3(normalizedDirection, right), -1.0f, 1.0f);
}

static float ComputeSourceVolumeLocked(const AudioSourceRecord* source) {
    if (!source) {
        return 0.0f;
    }

    const float gain = source->gain < 0.0f ? 0.0f : source->gain;
    const float listenerGain = g_listenerGain < 0.0f ? 0.0f : g_listenerGain;
    return ClampFloat(gain * listenerGain * ComputeDistanceAttenuation(source), 0.0f, 1.0f);
}

static void ApplySourceOutputMatrixLocked(AudioSourceRecord* source) {
    if (!source || !source->voice || !g_masterVoice || source->voiceChannels <= 0 || g_masterChannels <= 0) {
        return;
    }

    constexpr ALsizei kMatrixLimit = 64;
    const ALsizei sourceChannels = source->voiceChannels > kMatrixLimit ? kMatrixLimit : source->voiceChannels;
    const ALsizei destinationChannels = g_masterChannels > kMatrixLimit ? kMatrixLimit : g_masterChannels;
    float matrix[kMatrixLimit * kMatrixLimit] = {};

    if (sourceChannels == 1 && destinationChannels >= 2) {
        const float pan = ComputeSourcePanLocked(source);
        matrix[0] = std::sqrt((1.0f - pan) * 0.5f);
        matrix[1] = std::sqrt((1.0f + pan) * 0.5f);
    } else if (sourceChannels == 1) {
        matrix[0] = 1.0f;
    } else {
        const ALsizei commonChannels = sourceChannels < destinationChannels ? sourceChannels : destinationChannels;
        for (ALsizei i = 0; i < commonChannels; ++i) {
            matrix[(i * destinationChannels) + i] = 1.0f;
        }
    }

    source->voice->SetOutputMatrix(
        g_masterVoice,
        static_cast<UINT32>(sourceChannels),
        static_cast<UINT32>(destinationChannels),
        matrix);
}

static void ApplySourceVolumeLocked(AudioSourceRecord* source) {
    if (source && source->voice) {
        source->voice->SetVolume(ComputeSourceVolumeLocked(source));
        ApplySourceOutputMatrixLocked(source);
    }
}

static void ApplyAllSourceVolumesLocked() {
    for (auto& source : g_sources) {
        ApplySourceVolumeLocked(&source);
    }
}

static AudioBufferRecord* FindBuffer(ALuint id, bool create) {
    if (id == 0) {
        return nullptr;
    }

    AudioBufferRecord* empty = nullptr;
    for (auto& buffer : g_buffers) {
        if (buffer.id == id) {
            return &buffer;
        }
        if (!empty && buffer.id == 0) {
            empty = &buffer;
        }
    }

    if (!create || !empty) {
        return nullptr;
    }

    std::memset(empty, 0, sizeof(*empty));
    empty->id = id;
    return empty;
}

static AudioSourceRecord* FindSource(ALuint id, bool create) {
    if (id == 0) {
        return nullptr;
    }

    AudioSourceRecord* empty = nullptr;
    for (auto& source : g_sources) {
        if (source.id == id) {
            return &source;
        }
        if (!empty && source.id == 0) {
            empty = &source;
        }
    }

    if (!create || !empty) {
        return nullptr;
    }

    std::memset(empty, 0, sizeof(*empty));
    empty->id = id;
    empty->gain = 1.0f;
    empty->pitch = 1.0f;
    empty->rolloff = 1.0f;
    empty->referenceDistance = 1.0f;
    empty->maxDistance = 1000000.0f;
    empty->relative = AL_FALSE;
    empty->distanceModel = AL_NONE;
    empty->state = AL_INITIAL;
    return empty;
}

static void DestroyVoiceLocked(AudioSourceRecord* source, ALenum stateAfter = AL_STOPPED) {
    if (!source) {
        return;
    }
    if (source->voice) {
        source->voice->Stop(0);
        source->voice->FlushSourceBuffers();
        source->voice->DestroyVoice();
        source->voice = nullptr;
    }
    source->voiceChannels = 0;
    source->voiceWaveValid = false;
    if (source->queuedCount <= 0 || stateAfter == AL_INITIAL) {
        source->submittedCount = 0;
    } else if (stateAfter == AL_STOPPED) {
        source->submittedCount = source->queuedCount;
    }
    source->state = stateAfter;
}

static void UpdateSourceStateLocked(AudioSourceRecord* source) {
    if (!source || !source->voice || source->state != AL_PLAYING) {
        return;
    }

    XAUDIO2_VOICE_STATE voiceState = {};
    source->voice->GetState(&voiceState, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (voiceState.BuffersQueued == 0) {
        if (source->queuedCount > 0) {
            source->voice->Stop(0);
            source->state = AL_STOPPED;
        } else {
            DestroyVoiceLocked(source, AL_STOPPED);
        }
    }
}

static float ClampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static HRESULT SubmitBufferToVoice(AudioSourceRecord* source, AudioBufferRecord* buffer, bool endOfStream) {
    if (!source || !source->voice || !buffer || !buffer->data || buffer->bytes <= 0) {
        return E_INVALIDARG;
    }

    XAUDIO2_BUFFER xaBuffer = {};
    xaBuffer.AudioBytes = static_cast<UINT32>(buffer->bytes);
    xaBuffer.pAudioData = buffer->data;
    xaBuffer.Flags = endOfStream ? XAUDIO2_END_OF_STREAM : 0;
    if (source->looping && source->queuedCount <= 1) {
        xaBuffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    }
    return source->voice->SubmitSourceBuffer(&xaBuffer);
}

static ALsizei GetQueuedBuffersInVoiceLocked(AudioSourceRecord* source) {
    if (!source || !source->voice) {
        return 0;
    }

    XAUDIO2_VOICE_STATE state = {};
    source->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    ALsizei queuedInVoice = static_cast<ALsizei>(state.BuffersQueued);
    if (queuedInVoice > source->submittedCount) {
        queuedInVoice = source->submittedCount;
    }
    return queuedInVoice;
}

static ALsizei GetProcessedQueuedBufferCountLocked(AudioSourceRecord* source) {
    if (!source || source->queuedCount <= 0 || source->submittedCount <= 0) {
        return 0;
    }

    ALsizei processed = source->submittedCount - GetQueuedBuffersInVoiceLocked(source);
    if (processed < 0) {
        processed = 0;
    }
    if (processed > source->queuedCount) {
        processed = source->queuedCount;
    }
    return processed;
}

static bool SubmitQueuedBuffersLocked(AudioSourceRecord* source) {
    if (!source || !source->voice || !source->voiceWaveValid) {
        return false;
    }

    bool submittedAny = false;
    if (source->submittedCount < 0) {
        source->submittedCount = 0;
    }
    if (source->submittedCount > source->queuedCount) {
        source->submittedCount = source->queuedCount;
    }

    while (source->submittedCount < source->queuedCount) {
        auto* buffer = FindBuffer(source->queued[source->submittedCount], false);
        if (!buffer || !buffer->data || buffer->bytes <= 0 || !SameWaveFormat(buffer->wave, source->voiceWave)) {
            break;
        }

        HRESULT hr = SubmitBufferToVoice(source, buffer, false);
        if (FAILED(hr)) {
            break;
        }

        ++source->submittedCount;
        submittedAny = true;
    }

    return submittedAny;
}

static void LogBufferDataOnce(ALuint bufferId, ALenum format, ALsizei size, ALsizei frequency) {
    if (InterlockedIncrement(&g_bufferDataLogCount) > 24) {
        return;
    }

    char line[192] = {};
    std::snprintf(
        line,
        sizeof(line),
        "alBufferData buffer=%u format=0x%04X bytes=%d frequency=%d",
        bufferId,
        format,
        size,
        frequency);
    AudioLog(line);
}

static void LogSourcePlayOnce(ALuint sourceId, ALsizei playCount, HRESULT hr) {
    if (InterlockedIncrement(&g_sourcePlayLogCount) > 24) {
        return;
    }

    char line[192] = {};
    std::snprintf(
        line,
        sizeof(line),
        "alSourcePlay source=%u buffers=%d hr=0x%08X",
        sourceId,
        playCount,
        static_cast<unsigned>(hr));
    AudioLog(line);
}

static void LogQueuedPlayOnce(ALuint sourceId, ALsizei queuedCount, HRESULT hr) {
    if (InterlockedIncrement(&g_queuedPlayLogCount) > 24) {
        return;
    }

    char line[192] = {};
    std::snprintf(
        line,
        sizeof(line),
        "alSourcePlay queued source=%u buffers=%d hr=0x%08X",
        sourceId,
        queuedCount,
        static_cast<unsigned>(hr));
    AudioLog(line);
}

void* LookupAlProc(const ALchar* funcname);
void* LookupAlcProc(const ALCchar* funcname);

// -----------------------------------------------------------------------------
// ALC -- device + context management
// -----------------------------------------------------------------------------

__declspec(dllexport) void* __cdecl alcOpenDevice(const ALCchar* /*name*/) {
    LockAudio();
    const bool ready = EnsureAudioEngineLocked();
    UnlockAudio();
    return ready ? &g_deviceSentinel : nullptr;
}

__declspec(dllexport) ALCboolean __cdecl alcCloseDevice(void* /*device*/) {
    return ALC_TRUE;
}

__declspec(dllexport) void* __cdecl alcCreateContext(void* device, const ALCint* /*attrlist*/) {
    if (!device) {
        return nullptr;
    }

    LockAudio();
    const bool ready = EnsureAudioEngineLocked();
    UnlockAudio();
    return ready ? &g_contextSentinel : nullptr;
}

__declspec(dllexport) ALCboolean __cdecl alcMakeContextCurrent(void* context) {
    LockAudio();
    g_currentContext = context;
    UnlockAudio();
    return ALC_TRUE;
}

__declspec(dllexport) void* __cdecl alcGetCurrentContext(void) {
    return g_currentContext;
}

__declspec(dllexport) void* __cdecl alcGetContextsDevice(void* context) {
    return context ? &g_deviceSentinel : nullptr;
}

__declspec(dllexport) void __cdecl alcSuspendContext(void* /*context*/) {}
__declspec(dllexport) void __cdecl alcProcessContext(void* /*context*/) {}
__declspec(dllexport) void __cdecl alcDestroyContext(void* /*context*/) {}

__declspec(dllexport) ALCenum __cdecl alcGetError(void* /*device*/) {
    return ALC_NO_ERROR;
}

__declspec(dllexport) ALCboolean __cdecl alcIsExtensionPresent(void* /*device*/, const ALCchar* /*ext*/) {
    return ALC_FALSE;
}

__declspec(dllexport) void* __cdecl alcGetProcAddress(void* /*device*/, const ALCchar* funcname) {
    return LookupAlcProc(funcname);
}

__declspec(dllexport) ALCenum __cdecl alcGetEnumValue(void* /*device*/, const ALCchar* /*enumname*/) {
    return 0;
}

__declspec(dllexport) const ALCchar* __cdecl alcGetString(void* /*device*/, ALCenum param) {
    switch (param) {
        case ALC_DEFAULT_DEVICE_SPECIFIER:
        case ALC_DEVICE_SPECIFIER:
        case ALC_DEFAULT_ALL_DEVICES_SPECIFIER:
        case ALC_ALL_DEVICES_SPECIFIER:
            return kDeviceList;
        case ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER:
        case ALC_CAPTURE_DEVICE_SPECIFIER:
            return kEmptyList;
        case ALC_EXTENSIONS:
            return kAlcExtensions;
        default:
            return kAlcExtensions;
    }
}

__declspec(dllexport) void __cdecl alcGetIntegerv(void* /*device*/, ALCenum param, ALCsizei size, ALCint* values) {
    if (!values || size <= 0) return;
    for (ALCsizei i = 0; i < size; ++i) values[i] = 0;
    if (size >= 1) {
        switch (param) {
            case ALC_MAJOR_VERSION: values[0] = 1; break;
            case ALC_MINOR_VERSION: values[0] = 1; break;
            case ALC_FREQUENCY: values[0] = 48000; break;
            case ALC_REFRESH: values[0] = 60; break;
            case ALC_SYNC: values[0] = ALC_FALSE; break;
            case ALC_MONO_SOURCES: values[0] = 64; break;
            case ALC_STEREO_SOURCES: values[0] = 16; break;
            case ALC_ATTRIBUTES_SIZE: values[0] = 0; break;
            default: break;
        }
    }
}

__declspec(dllexport) void* __cdecl alcCaptureOpenDevice(const ALCchar* /*name*/, ALCuint /*freq*/, ALCenum /*fmt*/, ALCsizei /*bufsz*/) {
    return nullptr;
}

__declspec(dllexport) ALCboolean __cdecl alcCaptureCloseDevice(void* /*device*/) {
    return ALC_TRUE;
}

__declspec(dllexport) void __cdecl alcCaptureStart(void* /*device*/) {}
__declspec(dllexport) void __cdecl alcCaptureStop(void* /*device*/) {}
__declspec(dllexport) void __cdecl alcCaptureSamples(void* /*device*/, ALCvoid* /*buffer*/, ALCsizei /*samples*/) {}

// -----------------------------------------------------------------------------
// AL -- listener / sources / buffers
// -----------------------------------------------------------------------------

__declspec(dllexport) ALenum __cdecl alGetError(void) {
    return AL_NO_ERROR;
}

__declspec(dllexport) const ALchar* __cdecl alGetString(ALenum param) {
    switch (param) {
        case AL_VENDOR:     return kAlVendor;
        case AL_RENDERER:   return kAlRenderer;
        case AL_VERSION:    return kAlVersion;
        case AL_EXTENSIONS: return kAlExtensions;
        default:            return kAlExtensions;
    }
}

__declspec(dllexport) ALboolean __cdecl alIsExtensionPresent(const ALchar* ext) {
    if (ext) {
        if (std::strcmp(ext, "AL_EXT_source_distance_model") == 0 ||
            std::strcmp(ext, "AL_EXT_LINEAR_DISTANCE") == 0 ||
            std::strcmp(ext, "AL_EXT_EXPONENT_DISTANCE") == 0) {
            return AL_TRUE;
        }
    }
    return AL_FALSE;
}

__declspec(dllexport) void* __cdecl alGetProcAddress(const ALchar* funcname) {
    return LookupAlProc(funcname);
}

__declspec(dllexport) ALenum __cdecl alGetEnumValue(const ALchar* enumname) {
    if (!enumname) {
        return 0;
    }
    if (std::strcmp(enumname, "AL_SOURCE_DISTANCE_MODEL") == 0) {
        return AL_SOURCE_DISTANCE_MODEL;
    }
    if (std::strcmp(enumname, "AL_DISTANCE_MODEL") == 0) {
        return AL_DISTANCE_MODEL;
    }
    if (std::strcmp(enumname, "AL_ORIENTATION") == 0) {
        return AL_ORIENTATION;
    }
    if (std::strcmp(enumname, "AL_NONE") == 0) {
        return AL_NONE;
    }
    if (std::strcmp(enumname, "AL_INVERSE_DISTANCE") == 0) {
        return AL_INVERSE_DISTANCE;
    }
    if (std::strcmp(enumname, "AL_INVERSE_DISTANCE_CLAMPED") == 0) {
        return AL_INVERSE_DISTANCE_CLAMPED;
    }
    if (std::strcmp(enumname, "AL_LINEAR_DISTANCE") == 0) {
        return AL_LINEAR_DISTANCE;
    }
    if (std::strcmp(enumname, "AL_LINEAR_DISTANCE_CLAMPED") == 0) {
        return AL_LINEAR_DISTANCE_CLAMPED;
    }
    if (std::strcmp(enumname, "AL_EXPONENT_DISTANCE") == 0) {
        return AL_EXPONENT_DISTANCE;
    }
    if (std::strcmp(enumname, "AL_EXPONENT_DISTANCE_CLAMPED") == 0) {
        return AL_EXPONENT_DISTANCE_CLAMPED;
    }
    return 0;
}

__declspec(dllexport) void __cdecl alEnable(ALenum /*cap*/) {}
__declspec(dllexport) void __cdecl alDisable(ALenum /*cap*/) {}
__declspec(dllexport) ALboolean __cdecl alIsEnabled(ALenum /*cap*/) { return AL_FALSE; }

__declspec(dllexport) void __cdecl alGetBooleanv(ALenum /*param*/, ALboolean* values) { if (values) values[0] = AL_FALSE; }
__declspec(dllexport) void __cdecl alGetIntegerv(ALenum param, ALint* values) {
    if (!values) return;
    values[0] = 0;
    LockAudio();
    if (param == AL_DISTANCE_MODEL) {
        values[0] = g_distanceModel;
    }
    UnlockAudio();
}
__declspec(dllexport) void __cdecl alGetFloatv(ALenum /*param*/, ALfloat* values) { if (values) values[0] = 0.0f; }
__declspec(dllexport) void __cdecl alGetDoublev(ALenum /*param*/, ALdouble* values) { if (values) values[0] = 0.0; }

__declspec(dllexport) ALboolean __cdecl alGetBoolean(ALenum /*param*/) { return AL_FALSE; }
__declspec(dllexport) ALint __cdecl alGetInteger(ALenum param) {
    ALint value = 0;
    alGetIntegerv(param, &value);
    return value;
}
__declspec(dllexport) ALfloat __cdecl alGetFloat(ALenum /*param*/) { return 0.0f; }
__declspec(dllexport) ALdouble __cdecl alGetDouble(ALenum /*param*/) { return 0.0; }

// listener
__declspec(dllexport) void __cdecl alListenerf(ALenum param, ALfloat value) {
    LockAudio();
    if (param == AL_GAIN) {
        g_listenerGain = value < 0.0f ? 0.0f : value;
        ApplyAllSourceVolumesLocked();
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alListener3f(ALenum param, ALfloat x, ALfloat y, ALfloat z) {
    LockAudio();
    if (param == AL_POSITION) {
        g_listenerPosition = { x, y, z };
        ApplyAllSourceVolumesLocked();
    } else if (param == AL_VELOCITY) {
        g_listenerVelocity = { x, y, z };
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alListenerfv(ALenum param, const ALfloat* values) {
    if (!values) return;
    if (param == AL_POSITION || param == AL_VELOCITY) {
        alListener3f(param, values[0], values[1], values[2]);
    } else if (param == AL_GAIN) {
        alListenerf(param, values[0]);
    } else if (param == AL_ORIENTATION) {
        LockAudio();
        g_listenerAt = { values[0], values[1], values[2] };
        g_listenerUp = { values[3], values[4], values[5] };
        ApplyAllSourceVolumesLocked();
        UnlockAudio();
    }
}
__declspec(dllexport) void __cdecl alListeneri(ALenum, ALint) {}
__declspec(dllexport) void __cdecl alListener3i(ALenum, ALint, ALint, ALint) {}
__declspec(dllexport) void __cdecl alListeneriv(ALenum, const ALint*) {}

__declspec(dllexport) void __cdecl alGetListenerf(ALenum param, ALfloat* v) {
    if (!v) return;
    *v = 0.0f;
    LockAudio();
    if (param == AL_GAIN) {
        *v = g_listenerGain;
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alGetListener3f(ALenum param, ALfloat* a, ALfloat* b, ALfloat* c) {
    if (!a || !b || !c) return;
    *a = 0.0f; *b = 0.0f; *c = 0.0f;
    LockAudio();
    if (param == AL_POSITION) {
        *a = g_listenerPosition.x; *b = g_listenerPosition.y; *c = g_listenerPosition.z;
    } else if (param == AL_VELOCITY) {
        *a = g_listenerVelocity.x; *b = g_listenerVelocity.y; *c = g_listenerVelocity.z;
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alGetListenerfv(ALenum param, ALfloat* v) {
    if (!v) return;
    if (param == AL_POSITION || param == AL_VELOCITY) {
        alGetListener3f(param, &v[0], &v[1], &v[2]);
    } else if (param == AL_ORIENTATION) {
        LockAudio();
        v[0] = g_listenerAt.x; v[1] = g_listenerAt.y; v[2] = g_listenerAt.z;
        v[3] = g_listenerUp.x; v[4] = g_listenerUp.y; v[5] = g_listenerUp.z;
        UnlockAudio();
    } else {
        alGetListenerf(param, v);
    }
}
__declspec(dllexport) void __cdecl alGetListeneri(ALenum, ALint* v) { if (v) *v = 0; }
__declspec(dllexport) void __cdecl alGetListener3i(ALenum, ALint* a, ALint* b, ALint* c) {
    if (a) *a = 0; if (b) *b = 0; if (c) *c = 0;
}
__declspec(dllexport) void __cdecl alGetListeneriv(ALenum, ALint* v) { if (v) *v = 0; }

__declspec(dllexport) void __cdecl alGenSources(ALsizei n, ALuint* sources) {
    if (!sources || n <= 0) return;
    LockAudio();
    for (ALsizei i = 0; i < n; ++i) {
        const ALuint id = g_nextSourceId++;
        sources[i] = id;
        FindSource(id, true);
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alDeleteSources(ALsizei n, const ALuint* sources) {
    if (!sources || n <= 0) return;
    LockAudio();
    for (ALsizei i = 0; i < n; ++i) {
        auto* source = FindSource(sources[i], false);
        if (!source) {
            continue;
        }
        DestroyVoiceLocked(source, AL_STOPPED);
        std::memset(source, 0, sizeof(*source));
    }
    UnlockAudio();
}

__declspec(dllexport) ALboolean __cdecl alIsSource(ALuint name) {
    LockAudio();
    auto* source = FindSource(name, false);
    UnlockAudio();
    return source ? AL_TRUE : AL_FALSE;
}

__declspec(dllexport) void __cdecl alSourcef(ALuint sourceId, ALenum param, ALfloat value) {
    LockAudio();
    auto* source = FindSource(sourceId, true);
    if (source) {
        if (param == AL_GAIN) {
            source->gain = value < 0.0f ? 0.0f : value;
            ApplySourceVolumeLocked(source);
        } else if (param == AL_PITCH) {
            source->pitch = ClampFloat(value, 0.1f, XAUDIO2_MAX_FREQ_RATIO);
            if (source->voice) {
                source->voice->SetFrequencyRatio(source->pitch);
            }
        } else if (param == AL_ROLLOFF_FACTOR) {
            source->rolloff = value < 0.0f ? 0.0f : value;
            ApplySourceVolumeLocked(source);
        } else if (param == AL_REFERENCE_DISTANCE) {
            source->referenceDistance = value > 0.0f ? value : 1.0f;
            ApplySourceVolumeLocked(source);
        } else if (param == AL_MAX_DISTANCE) {
            source->maxDistance = value > 0.0f ? value : source->referenceDistance;
            ApplySourceVolumeLocked(source);
        }
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alSource3f(ALuint sourceId, ALenum param, ALfloat x, ALfloat y, ALfloat z) {
    LockAudio();
    auto* source = FindSource(sourceId, true);
    if (source) {
        if (param == AL_POSITION) {
            source->position = { x, y, z };
            ApplySourceVolumeLocked(source);
        } else if (param == AL_VELOCITY) {
            source->velocity = { x, y, z };
        }
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alSourcefv(ALuint source, ALenum param, const ALfloat* values) {
    if (values) {
        if (param == AL_POSITION || param == AL_VELOCITY) {
            alSource3f(source, param, values[0], values[1], values[2]);
        } else {
            alSourcef(source, param, values[0]);
        }
    }
}

__declspec(dllexport) void __cdecl alSourcei(ALuint sourceId, ALenum param, ALint value) {
    LockAudio();
    auto* source = FindSource(sourceId, true);
    if (source) {
        if (param == AL_BUFFER) {
            DestroyVoiceLocked(source, AL_STOPPED);
            source->buffer = static_cast<ALuint>(value);
            source->queuedCount = 0;
            source->submittedCount = 0;
            source->state = value ? AL_INITIAL : AL_STOPPED;
        } else if (param == AL_LOOPING) {
            source->looping = value ? AL_TRUE : AL_FALSE;
        } else if (param == AL_SOURCE_RELATIVE) {
            source->relative = value ? AL_TRUE : AL_FALSE;
            ApplySourceVolumeLocked(source);
        } else if (param == AL_SOURCE_DISTANCE_MODEL && IsDistanceModel(value)) {
            source->distanceModel = static_cast<ALenum>(value);
            ApplySourceVolumeLocked(source);
        }
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alSource3i(ALuint, ALenum, ALint, ALint, ALint) {}
__declspec(dllexport) void __cdecl alSourceiv(ALuint source, ALenum param, const ALint* values) {
    if (values) {
        alSourcei(source, param, values[0]);
    }
}

__declspec(dllexport) void __cdecl alGetSourcef(ALuint sourceId, ALenum param, ALfloat* v) {
    if (!v) return;
    *v = 0.0f;
    LockAudio();
    auto* source = FindSource(sourceId, false);
    if (source) {
        if (param == AL_GAIN) {
            *v = source->gain;
        } else if (param == AL_PITCH) {
            *v = source->pitch;
        } else if (param == AL_ROLLOFF_FACTOR) {
            *v = source->rolloff;
        } else if (param == AL_REFERENCE_DISTANCE) {
            *v = source->referenceDistance;
        } else if (param == AL_MAX_DISTANCE) {
            *v = source->maxDistance;
        }
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alGetSource3f(ALuint sourceId, ALenum param, ALfloat* a, ALfloat* b, ALfloat* c) {
    if (!a || !b || !c) return;
    *a = 0.0f; *b = 0.0f; *c = 0.0f;
    LockAudio();
    auto* source = FindSource(sourceId, false);
    if (source) {
        if (param == AL_POSITION) {
            *a = source->position.x; *b = source->position.y; *c = source->position.z;
        } else if (param == AL_VELOCITY) {
            *a = source->velocity.x; *b = source->velocity.y; *c = source->velocity.z;
        }
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alGetSourcefv(ALuint source, ALenum param, ALfloat* v) {
    if (!v) return;
    if (param == AL_POSITION || param == AL_VELOCITY) {
        alGetSource3f(source, param, &v[0], &v[1], &v[2]);
    } else {
        alGetSourcef(source, param, v);
    }
}

__declspec(dllexport) void __cdecl alGetSourcei(ALuint sourceId, ALenum param, ALint* v) {
    if (!v) return;
    *v = 0;
    LockAudio();
    auto* source = FindSource(sourceId, false);
    if (source) {
        UpdateSourceStateLocked(source);
        if (param == AL_SOURCE_STATE) {
            *v = source->state;
        } else if (param == AL_BUFFER) {
            *v = static_cast<ALint>(source->buffer);
        } else if (param == AL_LOOPING) {
            *v = source->looping;
        } else if (param == AL_SOURCE_RELATIVE) {
            *v = source->relative;
        } else if (param == AL_SOURCE_DISTANCE_MODEL) {
            *v = source->distanceModel;
        } else if (param == AL_BUFFERS_QUEUED) {
            *v = source->queuedCount;
        } else if (param == AL_BUFFERS_PROCESSED) {
            *v = GetProcessedQueuedBufferCountLocked(source);
        }
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alGetSource3i(ALuint, ALenum, ALint* a, ALint* b, ALint* c) {
    if (a) *a = 0; if (b) *b = 0; if (c) *c = 0;
}
__declspec(dllexport) void __cdecl alGetSourceiv(ALuint source, ALenum param, ALint* v) {
    alGetSourcei(source, param, v);
}

__declspec(dllexport) void __cdecl alSourcePlay(ALuint sourceId) {
    LockAudio();
    auto* source = FindSource(sourceId, false);
    if (!source || !EnsureAudioEngineLocked()) {
        UnlockAudio();
        return;
    }

    DestroyVoiceLocked(source, AL_STOPPED);

    if (source->queuedCount > 0) {
        AudioBufferRecord* first = nullptr;
        for (ALsizei i = 0; i < source->queuedCount; ++i) {
            first = FindBuffer(source->queued[i], false);
            if (first && first->data && first->bytes > 0) {
                break;
            }
            first = nullptr;
        }

        if (!first) {
            source->state = AL_STOPPED;
            UnlockAudio();
            return;
        }

        const float volume = ComputeSourceVolumeLocked(source);
        if (volume <= 0.001f) {
            source->state = AL_STOPPED;
            UnlockAudio();
            return;
        }

        HRESULT hr = g_xaudio->CreateSourceVoice(
            &source->voice,
            &first->wave,
            0,
            XAUDIO2_MAX_FREQ_RATIO,
            nullptr,
            nullptr,
            nullptr);
        if (FAILED(hr) || !source->voice) {
            char line[192] = {};
            std::snprintf(line, sizeof(line), "CreateSourceVoice queued failed hr=0x%08X", static_cast<unsigned>(hr));
            AudioLog(line);
            source->state = AL_STOPPED;
            UnlockAudio();
            return;
        }

        source->submittedCount = 0;
        source->voiceChannels = first->channels;
        source->voiceWave = first->wave;
        source->voiceWaveValid = true;
        source->voice->SetVolume(volume);
        ApplySourceOutputMatrixLocked(source);
        source->voice->SetFrequencyRatio(source->pitch);

        const bool submittedAny = SubmitQueuedBuffersLocked(source);
        hr = submittedAny ? source->voice->Start(0) : E_FAIL;
        LogQueuedPlayOnce(sourceId, source->submittedCount, hr);
        if (!submittedAny || FAILED(hr)) {
            DestroyVoiceLocked(source, AL_STOPPED);
            UnlockAudio();
            return;
        }

        source->state = AL_PLAYING;
        UnlockAudio();
        return;
    }

    ALuint playList[kMaxQueuedBuffers] = {};
    ALsizei playCount = 0;
    if (source->buffer != 0) {
        playList[0] = source->buffer;
        playCount = 1;
    }

    AudioBufferRecord* first = nullptr;
    for (ALsizei i = 0; i < playCount; ++i) {
        first = FindBuffer(playList[i], false);
        if (first && first->data && first->bytes > 0) {
            break;
        }
        first = nullptr;
    }

    if (!first) {
        source->state = AL_STOPPED;
        UnlockAudio();
        return;
    }

    const float volume = ComputeSourceVolumeLocked(source);
    if (volume <= 0.001f) {
        source->state = AL_STOPPED;
        UnlockAudio();
        return;
    }

    HRESULT hr = g_xaudio->CreateSourceVoice(
        &source->voice,
        &first->wave,
        0,
        XAUDIO2_MAX_FREQ_RATIO,
        nullptr,
        nullptr,
        nullptr);
    if (FAILED(hr) || !source->voice) {
        char line[192] = {};
        std::snprintf(line, sizeof(line), "CreateSourceVoice failed hr=0x%08X", static_cast<unsigned>(hr));
        AudioLog(line);
        source->state = AL_STOPPED;
        UnlockAudio();
        return;
    }

    source->voiceChannels = first->channels;
    source->voice->SetVolume(volume);
    ApplySourceOutputMatrixLocked(source);
    source->voice->SetFrequencyRatio(source->pitch);

    bool submittedAny = false;
    for (ALsizei i = 0; i < playCount; ++i) {
        auto* buffer = FindBuffer(playList[i], false);
        if (!buffer || !buffer->data || buffer->bytes <= 0 || !SameWaveFormat(buffer->wave, first->wave)) {
            continue;
        }
        const bool endOfStream = i == playCount - 1;
        hr = SubmitBufferToVoice(source, buffer, endOfStream);
        if (SUCCEEDED(hr)) {
            submittedAny = true;
        }
    }

    hr = submittedAny ? source->voice->Start(0) : E_FAIL;
    LogSourcePlayOnce(sourceId, playCount, hr);
    if (!submittedAny || FAILED(hr)) {
        DestroyVoiceLocked(source, AL_STOPPED);
        UnlockAudio();
        return;
    }

    source->state = AL_PLAYING;
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alSourceStop(ALuint sourceId) {
    LockAudio();
    DestroyVoiceLocked(FindSource(sourceId, false), AL_STOPPED);
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alSourcePause(ALuint sourceId) {
    LockAudio();
    auto* source = FindSource(sourceId, false);
    if (source && source->voice) {
        source->voice->Stop(0);
        source->state = AL_PAUSED;
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alSourceRewind(ALuint sourceId) {
    LockAudio();
    DestroyVoiceLocked(FindSource(sourceId, false), AL_INITIAL);
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alSourcePlayv(ALsizei n, const ALuint* sources) {
    if (!sources || n <= 0) return;
    for (ALsizei i = 0; i < n; ++i) {
        alSourcePlay(sources[i]);
    }
}

__declspec(dllexport) void __cdecl alSourceStopv(ALsizei n, const ALuint* sources) {
    if (!sources || n <= 0) return;
    for (ALsizei i = 0; i < n; ++i) {
        alSourceStop(sources[i]);
    }
}

__declspec(dllexport) void __cdecl alSourcePausev(ALsizei n, const ALuint* sources) {
    if (!sources || n <= 0) return;
    for (ALsizei i = 0; i < n; ++i) {
        alSourcePause(sources[i]);
    }
}

__declspec(dllexport) void __cdecl alSourceRewindv(ALsizei n, const ALuint* sources) {
    if (!sources || n <= 0) return;
    for (ALsizei i = 0; i < n; ++i) {
        alSourceRewind(sources[i]);
    }
}

__declspec(dllexport) void __cdecl alSourceQueueBuffers(ALuint sourceId, ALsizei n, const ALuint* buffers) {
    if (!buffers || n <= 0) return;
    LockAudio();
    auto* source = FindSource(sourceId, true);
    if (source) {
        source->buffer = 0;
        for (ALsizei i = 0; i < n && source->queuedCount < kMaxQueuedBuffers; ++i) {
            source->queued[source->queuedCount++] = buffers[i];
        }
        if (source->voice && source->state == AL_PLAYING) {
            SubmitQueuedBuffersLocked(source);
            source->voice->Start(0);
        }
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alSourceUnqueueBuffers(ALuint sourceId, ALsizei n, ALuint* buffers) {
    if (!buffers || n <= 0) return;
    LockAudio();
    auto* source = FindSource(sourceId, false);
    if (!source) {
        for (ALsizei i = 0; i < n; ++i) buffers[i] = 0;
        UnlockAudio();
        return;
    }

    const ALsizei processed = GetProcessedQueuedBufferCountLocked(source);

    const ALsizei toRemove = n < processed ? n : processed;
    for (ALsizei i = 0; i < n; ++i) {
        buffers[i] = i < toRemove ? source->queued[i] : 0;
    }
    if (toRemove > 0) {
        for (ALsizei i = toRemove; i < source->queuedCount; ++i) {
            source->queued[i - toRemove] = source->queued[i];
        }
        source->queuedCount -= toRemove;
        source->submittedCount -= toRemove;
        if (source->submittedCount < 0) {
            source->submittedCount = 0;
        }
    }
    if (source->queuedCount == 0 && source->voice && GetQueuedBuffersInVoiceLocked(source) == 0) {
        DestroyVoiceLocked(source, AL_STOPPED);
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alGenBuffers(ALsizei n, ALuint* buffers) {
    if (!buffers || n <= 0) return;
    LockAudio();
    for (ALsizei i = 0; i < n; ++i) {
        const ALuint id = g_nextBufferId++;
        buffers[i] = id;
        FindBuffer(id, true);
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alDeleteBuffers(ALsizei n, const ALuint* buffers) {
    if (!buffers || n <= 0) return;
    LockAudio();
    for (ALsizei i = 0; i < n; ++i) {
        auto* buffer = FindBuffer(buffers[i], false);
        if (!buffer) {
            continue;
        }
        if (buffer->data) {
            HeapFree(GetProcessHeap(), 0, buffer->data);
        }
        std::memset(buffer, 0, sizeof(*buffer));
    }
    UnlockAudio();
}

__declspec(dllexport) ALboolean __cdecl alIsBuffer(ALuint name) {
    LockAudio();
    auto* buffer = FindBuffer(name, false);
    UnlockAudio();
    return buffer ? AL_TRUE : AL_FALSE;
}

__declspec(dllexport) void __cdecl alBufferData(ALuint bufferId, ALenum format, const ALvoid* data, ALsizei size, ALsizei frequency) {
    if (size < 0) {
        return;
    }

    LockAudio();
    auto* buffer = FindBuffer(bufferId, true);
    if (!buffer) {
        UnlockAudio();
        return;
    }

    WAVEFORMATEX wave = {};
    ALsizei channels = 0;
    ALsizei bits = 0;
    if (!FormatToWave(format, frequency, wave, channels, bits)) {
        char line[192] = {};
        std::snprintf(line, sizeof(line), "unsupported alBufferData format=0x%04X freq=%d", format, frequency);
        AudioLog(line);
        UnlockAudio();
        return;
    }

    uint8_t* copy = nullptr;
    if (size > 0) {
        copy = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(size)));
        if (!copy) {
            UnlockAudio();
            return;
        }
        if (data) {
            std::memcpy(copy, data, static_cast<size_t>(size));
        } else {
            std::memset(copy, 0, static_cast<size_t>(size));
        }
    }

    if (buffer->data) {
        HeapFree(GetProcessHeap(), 0, buffer->data);
    }
    buffer->format = format;
    buffer->frequency = frequency;
    buffer->bytes = size;
    buffer->channels = channels;
    buffer->bits = bits;
    buffer->data = copy;
    buffer->wave = wave;
    LogBufferDataOnce(bufferId, format, size, frequency);
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alBufferf(ALuint, ALenum, ALfloat) {}
__declspec(dllexport) void __cdecl alBuffer3f(ALuint, ALenum, ALfloat, ALfloat, ALfloat) {}
__declspec(dllexport) void __cdecl alBufferfv(ALuint, ALenum, const ALfloat*) {}
__declspec(dllexport) void __cdecl alBufferi(ALuint, ALenum, ALint) {}
__declspec(dllexport) void __cdecl alBuffer3i(ALuint, ALenum, ALint, ALint, ALint) {}
__declspec(dllexport) void __cdecl alBufferiv(ALuint, ALenum, const ALint*) {}
__declspec(dllexport) void __cdecl alGetBufferf(ALuint, ALenum, ALfloat* v) { if (v) *v = 0.0f; }
__declspec(dllexport) void __cdecl alGetBuffer3f(ALuint, ALenum, ALfloat* a, ALfloat* b, ALfloat* c) {
    if (a) *a = 0.0f; if (b) *b = 0.0f; if (c) *c = 0.0f;
}
__declspec(dllexport) void __cdecl alGetBufferfv(ALuint, ALenum, ALfloat* v) { if (v) *v = 0.0f; }
__declspec(dllexport) void __cdecl alGetBufferi(ALuint bufferId, ALenum param, ALint* v) {
    if (!v) return;
    *v = 0;
    LockAudio();
    auto* buffer = FindBuffer(bufferId, false);
    if (buffer) {
        switch (param) {
            case AL_FREQUENCY: *v = buffer->frequency; break;
            case AL_BITS: *v = buffer->bits; break;
            case AL_CHANNELS: *v = buffer->channels; break;
            case AL_SIZE: *v = buffer->bytes; break;
            default: break;
        }
    }
    UnlockAudio();
}

__declspec(dllexport) void __cdecl alGetBuffer3i(ALuint, ALenum, ALint* a, ALint* b, ALint* c) {
    if (a) *a = 0; if (b) *b = 0; if (c) *c = 0;
}
__declspec(dllexport) void __cdecl alGetBufferiv(ALuint buffer, ALenum param, ALint* v) {
    alGetBufferi(buffer, param, v);
}

__declspec(dllexport) void __cdecl alDopplerFactor(ALfloat) {}
__declspec(dllexport) void __cdecl alDopplerVelocity(ALfloat) {}
__declspec(dllexport) void __cdecl alSpeedOfSound(ALfloat) {}
__declspec(dllexport) void __cdecl alDistanceModel(ALenum model) {
    if (!IsDistanceModel(model)) {
        return;
    }

    LockAudio();
    g_distanceModel = model;
    ApplyAllSourceVolumesLocked();
    UnlockAudio();
}

struct ProcEntry {
    const char* name;
    void* proc;
};

#define AL_PROC(name) { #name, reinterpret_cast<void*>(name) }

void* LookupAlProc(const ALchar* funcname) {
    if (!funcname || !funcname[0]) {
        return nullptr;
    }

    static const ProcEntry procs[] = {
        AL_PROC(alEnable),
        AL_PROC(alDisable),
        AL_PROC(alIsEnabled),
        AL_PROC(alGetString),
        AL_PROC(alGetBooleanv),
        AL_PROC(alGetIntegerv),
        AL_PROC(alGetFloatv),
        AL_PROC(alGetDoublev),
        AL_PROC(alGetBoolean),
        AL_PROC(alGetInteger),
        AL_PROC(alGetFloat),
        AL_PROC(alGetDouble),
        AL_PROC(alGetError),
        AL_PROC(alIsExtensionPresent),
        AL_PROC(alGetProcAddress),
        AL_PROC(alGetEnumValue),
        AL_PROC(alListenerf),
        AL_PROC(alListener3f),
        AL_PROC(alListenerfv),
        AL_PROC(alListeneri),
        AL_PROC(alListener3i),
        AL_PROC(alListeneriv),
        AL_PROC(alGetListenerf),
        AL_PROC(alGetListener3f),
        AL_PROC(alGetListenerfv),
        AL_PROC(alGetListeneri),
        AL_PROC(alGetListener3i),
        AL_PROC(alGetListeneriv),
        AL_PROC(alGenSources),
        AL_PROC(alDeleteSources),
        AL_PROC(alIsSource),
        AL_PROC(alSourcef),
        AL_PROC(alSource3f),
        AL_PROC(alSourcefv),
        AL_PROC(alSourcei),
        AL_PROC(alSource3i),
        AL_PROC(alSourceiv),
        AL_PROC(alGetSourcef),
        AL_PROC(alGetSource3f),
        AL_PROC(alGetSourcefv),
        AL_PROC(alGetSourcei),
        AL_PROC(alGetSource3i),
        AL_PROC(alGetSourceiv),
        AL_PROC(alSourcePlay),
        AL_PROC(alSourceStop),
        AL_PROC(alSourcePause),
        AL_PROC(alSourceRewind),
        AL_PROC(alSourcePlayv),
        AL_PROC(alSourceStopv),
        AL_PROC(alSourcePausev),
        AL_PROC(alSourceRewindv),
        AL_PROC(alSourceQueueBuffers),
        AL_PROC(alSourceUnqueueBuffers),
        AL_PROC(alGenBuffers),
        AL_PROC(alDeleteBuffers),
        AL_PROC(alIsBuffer),
        AL_PROC(alBufferData),
        AL_PROC(alBufferf),
        AL_PROC(alBuffer3f),
        AL_PROC(alBufferfv),
        AL_PROC(alBufferi),
        AL_PROC(alBuffer3i),
        AL_PROC(alBufferiv),
        AL_PROC(alGetBufferf),
        AL_PROC(alGetBuffer3f),
        AL_PROC(alGetBufferfv),
        AL_PROC(alGetBufferi),
        AL_PROC(alGetBuffer3i),
        AL_PROC(alGetBufferiv),
        AL_PROC(alDopplerFactor),
        AL_PROC(alDopplerVelocity),
        AL_PROC(alSpeedOfSound),
        AL_PROC(alDistanceModel),
    };

    for (const auto& proc : procs) {
        if (std::strcmp(funcname, proc.name) == 0) {
            return proc.proc;
        }
    }

    return nullptr;
}

void* LookupAlcProc(const ALCchar* funcname) {
    if (!funcname || !funcname[0]) {
        return nullptr;
    }

    static const ProcEntry procs[] = {
        AL_PROC(alcOpenDevice),
        AL_PROC(alcCloseDevice),
        AL_PROC(alcCreateContext),
        AL_PROC(alcMakeContextCurrent),
        AL_PROC(alcGetCurrentContext),
        AL_PROC(alcGetContextsDevice),
        AL_PROC(alcSuspendContext),
        AL_PROC(alcProcessContext),
        AL_PROC(alcDestroyContext),
        AL_PROC(alcGetError),
        AL_PROC(alcIsExtensionPresent),
        AL_PROC(alcGetProcAddress),
        AL_PROC(alcGetEnumValue),
        AL_PROC(alcGetString),
        AL_PROC(alcGetIntegerv),
        AL_PROC(alcCaptureOpenDevice),
        AL_PROC(alcCaptureCloseDevice),
        AL_PROC(alcCaptureStart),
        AL_PROC(alcCaptureStop),
        AL_PROC(alcCaptureSamples),
    };

    for (const auto& proc : procs) {
        if (std::strcmp(funcname, proc.name) == 0) {
            return proc.proc;
        }
    }

    return nullptr;
}

#undef AL_PROC

} // extern "C"
