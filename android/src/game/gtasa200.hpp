/*
 *  Mod Loader - Android port
 *      Everything the port knows about GTA San Andreas for Android 2.00 (libGTASA.so, ARMv7).
 *
 *      The library exports its C++ symbols, so the port hooks and reads game data BY NAME and does not depend on
 *      hard coded offsets. The few struct layouts below were checked against the disassembly of libGTASA.so:
 *
 *        CStreamingInfo (20 bytes)   - CStreamingInfo::SetCdPosnAndSize writes {+8 posn, +12 size};
 *                                      GetCdPosnAndSize reads imgId at +7 and ms_files[imgId].streamHandle (+0x2c).
 *        StreamingFile  (48 bytes)   - CStreaming::AddImageToList / InitImageList: name[40] at +0, flag at +0x28,
 *                                      stream handle at +0x2c.
 *        IMG directory entry (32 b)  - read by CStreaming::LoadCdDirectory: {u32 offset, u16 streamingSize,
 *                                      u16 sizeInArchive, char name[24]}.
 *
 *      Before hooking anything the first bytes of each function are compared with the ones of the analysed library,
 *      so a different build of the game is detected and the affected feature is simply left disabled.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include <cstdint>
#include <cstddef>

namespace mla { namespace gtasa {

/* ---- struct layouts ---- */
struct CStreamingInfo
{
    int16_t     nextIndex;
    int16_t     prevIndex;
    int16_t     nextIndexOnCd;
    uint8_t     flags;
    uint8_t     imgId;          // index in CStreaming::ms_files
    uint32_t    cdPosn;         // in sectors (2048 bytes)
    uint32_t    cdSize;         // in sectors, 0 = the model is not in any image
    uint8_t     loadState;
    uint8_t     pad[3];
};

struct StreamingFile
{
    char        name[40];
    uint8_t     notPlayerImg;   // the images flagged with 1 get their directory loaded by CStreaming::Init2
    uint8_t     pad[3];
    int32_t     streamHandle;   // (CdStream slot << 24)
};

struct ImgDirEntry
{
    uint32_t    offset;         // in sectors
    uint16_t    streamingSize;  // in sectors
    uint16_t    sizeInArchive;  // in sectors (0 on most images)
    char        name[24];
};

static_assert(sizeof(CStreamingInfo) == 20, "CStreamingInfo layout");
static_assert(offsetof(CStreamingInfo, imgId) == 7 && offsetof(CStreamingInfo, cdPosn) == 8 && offsetof(CStreamingInfo, cdSize) == 12, "CStreamingInfo layout");
static_assert(sizeof(StreamingFile) == 48 && offsetof(StreamingFile, notPlayerImg) == 0x28 && offsetof(StreamingFile, streamHandle) == 0x2c, "StreamingFile layout");
static_assert(sizeof(ImgDirEntry) == 32, "IMG directory entry layout");

enum : unsigned
{
    kSectorSize         = 2048,
    kMaxStreamingFiles  = 8,        // CStreaming::ms_files has 8 slots
    kNumStreamingInfos  = 26316,    // CStreaming::ms_aInfoForModel is 0x807F0 bytes = 26316 * 20
    kMaxImageSlotForOffsets = 6,    // CStreaming::ms_imageOffsets has 6 entries
};

/* ---- OS_FileOpen (the one place every file read of the game goes through) ---- */
enum OSFileAccess { kAccessRead = 0, kAccessWrite = 1, kAccessAppend = 2, kAccessReadAsync = 3 };
enum { kAreaUser = 1 };             // OSFileDataArea 1 = saves/settings (written to the user storage), never redirected

typedef int         (*fnOS_FileOpen)(int area, void** outHandle, const char* name, int access);
/* OS_FileRead returns 0 when it read everything that was asked, 2 at the end of the file and 3 on errors (NOT the byte count) */
typedef int         (*fnOS_FileRead)(void* handle, void* buffer, int size);
typedef void        (*fnOS_FileClose)(void* handle);
typedef unsigned    (*fnCFileMgr_OpenFile)(const char* path, const char* mode);
typedef void        (*fnCFileMgr_CloseFile)(unsigned handle);
typedef int         (*fnAddImageToList)(const char* path, bool notPlayerImg);
typedef void        (*fnLoadCdDirectory)(const char* path, int imageIndex);

/* ---- mangled names of the exports we use ---- */
namespace sym
{
    static const char* const OS_FileOpen            = "_Z11OS_FileOpen14OSFileDataAreaPPvPKc16OSFileAccessType";
    static const char* const OS_FileRead            = "_Z11OS_FileReadPvS_i";
    static const char* const OS_FileClose           = "_Z12OS_FileClosePv";
    static const char* const CFileMgr_OpenFile      = "_ZN8CFileMgr8OpenFileEPKcS1_";
    static const char* const CFileMgr_CloseFile     = "_ZN8CFileMgr9CloseFileEj";
    static const char* const AddImageToList         = "_ZN10CStreaming14AddImageToListEPKcb";
    static const char* const LoadCdDirectory        = "_ZN10CStreaming15LoadCdDirectoryEPKci";
    static const char* const ms_files               = "_ZN10CStreaming8ms_filesE";
    static const char* const ms_aInfoForModel       = "_ZN10CStreaming16ms_aInfoForModelE";
    static const char* const ms_streamingBufferSize = "_ZN10CStreaming22ms_streamingBufferSizeE";
}

/* ---- prologue fingerprints (first 8 bytes of the Thumb-2 code of libGTASA.so 2.00) ---- */
struct Fingerprint { const char* name; const char* symbol; uint8_t bytes[8]; };

extern const Fingerprint kFp_OS_FileOpen;
extern const Fingerprint kFp_OS_FileRead;
extern const Fingerprint kFp_LoadCdDirectory;

/* Compares the code at the given (possibly Thumb tagged) address with a fingerprint */
bool MatchesFingerprint(uintptr_t addr, const Fingerprint& fp);

/* Unit tests only: they hand the plugins ordinary C++ functions, which of course do not start like the ones of libGTASA.so */
void SetFingerprintChecks(bool enabled);

}} // namespace mla::gtasa
