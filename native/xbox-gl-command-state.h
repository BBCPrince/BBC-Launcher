#pragma once

#include <windows.h>
#include <cstdint>

static const wchar_t* kMinecraftXboxGlCommandStateName = L"Local\\MinecraftXboxGLCommandStateV12";
static constexpr LONG kMinecraftXboxGlCommandStateMagic = 0x58474C44; // XGLD
static constexpr LONG kMinecraftXboxGlCommandStateVersion = 12;

static constexpr unsigned int kMinecraftXboxGlColorBufferBit = 0x00004000u;
static constexpr unsigned int kMinecraftXboxTextureSampleMaxBytes = 16u * 1024u * 1024u;
static constexpr unsigned int kMinecraftXboxGuiFramebufferMaxWidth = 960u;
static constexpr unsigned int kMinecraftXboxGuiFramebufferMaxHeight = 540u;
static constexpr unsigned int kMinecraftXboxGuiFramebufferMaxBytes =
    kMinecraftXboxGuiFramebufferMaxWidth * kMinecraftXboxGuiFramebufferMaxHeight * 4u;

struct MinecraftXboxGlCommandState
{
    volatile LONG magic;
    volatile LONG version;
    volatile LONG64 clearSerial;
    volatile LONG64 viewportSerial;
    volatile LONG64 drawSerial;
    volatile LONG64 flushSerial;
    volatile LONG64 textureBindSerial;
    volatile LONG64 textureUploadSerial;
    volatile LONG64 textureSubUploadSerial;
    volatile LONG64 textureParameterSerial;
    volatile LONG64 bufferBindSerial;
    volatile LONG64 bufferUploadSerial;
    volatile LONG64 bufferSubUploadSerial;
    volatile LONG64 vertexArrayBindSerial;
    volatile LONG64 vertexAttribSerial;
    volatile LONG64 programUseSerial;
    volatile LONG64 uniformSerial;
    volatile LONG64 samplerBindSerial;
    volatile LONG64 textureSampleSerial;
    volatile LONG64 guiFramebufferSerial;
    volatile LONG64 guiTexturedTriangleSerial;
    volatile LONG64 guiColorTriangleSerial;
    volatile LONG64 guiSkippedVertexSerial;
    volatile LONG64 textureUploadBytes;
    volatile LONG64 bufferUploadBytes;
    volatile LONG64 drawCallTimeUs;
    volatile LONG64 textureUploadCallTimeUs;
    volatile LONG64 bufferUploadCallTimeUs;
    volatile LONG64 programUseCallTimeUs;
    volatile LONG64 uniformCallTimeUs;
    volatile LONG64 textureBindCallTimeUs;
    volatile LONG64 framebufferCallTimeUs;
    volatile LONG64 syncCallTimeUs;
    volatile LONG64 fenceSyncCallTimeUs;
    volatile LONG64 clientWaitSyncCallTimeUs;
    volatile LONG64 waitSyncCallTimeUs;
    volatile LONG64 deleteSyncCallTimeUs;
    volatile LONG64 isSyncCallTimeUs;
    volatile LONG64 flushCallTimeUs;

    float clearColor[4];
    unsigned int clearMask;

    int viewport[4];

    unsigned int lastDrawMode;
    int lastDrawFirst;
    int lastDrawCount;
    unsigned int lastDrawType;

    unsigned int activeTextureUnit;
    unsigned int lastTextureTarget;
    unsigned int lastTextureName;
    int lastTextureLevel;
    int lastTextureWidth;
    int lastTextureHeight;
    int lastTextureDepth;
    int lastTextureInternalFormat;
    unsigned int lastTextureFormat;
    unsigned int lastTextureType;
    LONG64 lastTextureUploadBytes;
    volatile LONG textureSampleReady;
    int textureSampleWidth;
    int textureSampleHeight;
    unsigned int textureSampleSourceFormat;
    unsigned int textureSampleSourceType;
    unsigned int textureSampleTextureName;
    LONG64 textureSampleBytes;
    volatile LONG guiFramebufferReady;
    int guiFramebufferWidth;
    int guiFramebufferHeight;
    LONG64 guiFramebufferBytes;
    unsigned int guiLastTextureName;
    int guiLastTextureWidth;
    int guiLastTextureHeight;
    unsigned int guiLastDrawMode;
    int guiLastDrawCount;
    unsigned int textureLastStoredName;
    int textureLastStoredWidth;
    int textureLastStoredHeight;
    unsigned int textureLargestName;
    int textureLargestWidth;
    int textureLargestHeight;
    unsigned int textureBestGuiName;
    int textureBestGuiWidth;
    int textureBestGuiHeight;
    LONG64 textureRecordCount;
    volatile LONG64 textureTableFullSerial;
    unsigned int textureExactGuiName;
    int textureExactGuiWidth;
    int textureExactGuiHeight;
    volatile LONG textureExactGuiHasPixels;
    unsigned int textureBestAllocatedGuiName;
    int textureBestAllocatedGuiWidth;
    int textureBestAllocatedGuiHeight;
    unsigned int textureLastAllocationName;
    int textureLastAllocationWidth;
    int textureLastAllocationHeight;
    volatile LONG64 textureShrinkPreservedSerial;
    unsigned int textureLastAttemptName;
    int textureLastAttemptLevel;
    int textureLastAttemptX;
    int textureLastAttemptY;
    int textureLastAttemptWidth;
    int textureLastAttemptHeight;
    unsigned int textureLastAttemptFormat;
    unsigned int textureLastAttemptType;
    unsigned int textureLastAttemptReason;
    unsigned int textureLastAttemptPbo;
    unsigned int textureLastAttemptUnit;
    volatile LONG64 textureExactGuiUploadAttemptSerial;
    volatile LONG64 textureExactGuiUploadAcceptedSerial;
    volatile LONG64 textureExactGuiUploadRejectedSerial;
    unsigned int textureExactGuiLastRejectReason;
    volatile LONG64 framebufferBindSerial;
    volatile LONG64 framebufferAttachSerial;
    volatile LONG64 framebufferBlitSerial;
    unsigned int framebufferDrawName;
    unsigned int framebufferReadName;
    unsigned int framebufferColorTextureName;
    int framebufferColorTextureWidth;
    int framebufferColorTextureHeight;
    unsigned int framebufferLastBlitSourceTextureName;
    unsigned int framebufferLastBlitDestTextureName;
    int framebufferLastBlitWidth;
    int framebufferLastBlitHeight;
    volatile LONG64 guiCandidateAcceptedSerial;
    volatile LONG64 guiCandidateRejectedSerial;
    unsigned int guiCandidateLastSourceTextureName;
    int guiCandidateLastSourceWidth;
    int guiCandidateLastSourceHeight;
    int guiCandidateLastWidth;
    int guiCandidateLastHeight;
    LONG64 guiCandidateLastPixels;
    LONG64 guiCandidateLastNonBlackPixels;
    LONG64 guiCandidateLastNonTransparentPixels;
    LONG64 guiCandidateLastAverageRed;
    LONG64 guiCandidateLastAverageGreen;
    LONG64 guiCandidateLastAverageBlue;
    unsigned int guiCandidateLastRejectReason;
    unsigned int guiCandidateAcceptedSourceTextureName;
    int guiCandidateAcceptedSourceWidth;
    int guiCandidateAcceptedSourceHeight;

    unsigned int lastBufferTarget;
    unsigned int lastBufferName;
    LONG64 lastBufferSize;
    unsigned int lastBufferUsage;

    unsigned int lastVertexArray;
    unsigned int lastVertexAttribIndex;
    int lastVertexAttribSize;
    unsigned int lastVertexAttribType;
    unsigned int lastVertexAttribNormalized;
    int lastVertexAttribStride;
    unsigned long long lastVertexAttribPointer;

    unsigned int currentProgram;
    int lastUniformLocation;

    unsigned char textureSampleRgba[kMinecraftXboxTextureSampleMaxBytes];
    unsigned char guiFramebufferRgba[kMinecraftXboxGuiFramebufferMaxBytes];
};

inline bool MinecraftXboxIsGlCommandStateReady(const MinecraftXboxGlCommandState* state)
{
    return state &&
        state->magic == kMinecraftXboxGlCommandStateMagic &&
        state->version == kMinecraftXboxGlCommandStateVersion;
}

inline void MinecraftXboxInitializeGlCommandState(MinecraftXboxGlCommandState* state)
{
    if (!state || MinecraftXboxIsGlCommandStateReady(state))
    {
        return;
    }

    ZeroMemory(state, sizeof(*state));
    state->clearColor[3] = 1.0f;
    state->version = kMinecraftXboxGlCommandStateVersion;
    MemoryBarrier();
    state->magic = kMinecraftXboxGlCommandStateMagic;
}
