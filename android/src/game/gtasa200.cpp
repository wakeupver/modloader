/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "gtasa200.hpp"
#include <cstring>

namespace mla { namespace gtasa {

// Taken from the disassembly of the libGTASA.so that came with the request (sha256 46c1a97f...06b9db)
const Fingerprint kFp_OS_FileOpen     = { "OS_FileOpen",              sym::OS_FileOpen,     { 0xF0, 0xB5, 0x03, 0xAF, 0x2D, 0xE9, 0x00, 0x0F } };
const Fingerprint kFp_OS_FileRead     = { "OS_FileRead",              sym::OS_FileRead,     { 0xF0, 0xB5, 0x03, 0xAF, 0x2D, 0xE9, 0x00, 0x07 } };
const Fingerprint kFp_LoadCdDirectory = { "CStreaming::LoadCdDirectory", sym::LoadCdDirectory, { 0xF0, 0xB5, 0x03, 0xAF, 0x2D, 0xE9, 0x00, 0x0F } };

static bool g_fingerprintChecks = true;

void SetFingerprintChecks(bool enabled) { g_fingerprintChecks = enabled; }

bool MatchesFingerprint(uintptr_t addr, const Fingerprint& fp)
{
    if(!addr) return false;
    if(!g_fingerprintChecks) return true;
    const uint8_t* code = (const uint8_t*)(addr & ~(uintptr_t)1);     // drop the Thumb bit
    return std::memcmp(code, fp.bytes, sizeof(fp.bytes)) == 0;
}

}} // namespace mla::gtasa
