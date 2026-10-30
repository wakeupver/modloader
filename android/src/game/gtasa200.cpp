/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "gtasa200.hpp"
#include "../util/log.hpp"

#include <cstdio>
#include <cstring>

namespace mla { namespace gtasa {

// Taken from the disassembly of the libGTASA.so that came with the request (sha256 46c1a97f...06b9db)
const Fingerprint kFp_OS_FileOpen     = { "OS_FileOpen",              sym::OS_FileOpen,     { 0xF0, 0xB5, 0x03, 0xAF, 0x2D, 0xE9, 0x00, 0x0F } };
const Fingerprint kFp_OS_FileRead     = { "OS_FileRead",              sym::OS_FileRead,     { 0xF0, 0xB5, 0x03, 0xAF, 0x2D, 0xE9, 0x00, 0x07 } };
const Fingerprint kFp_LoadCdDirectory = { "CStreaming::LoadCdDirectory", sym::LoadCdDirectory, { 0xF0, 0xB5, 0x03, 0xAF, 0x2D, 0xE9, 0x00, 0x0F } };

static bool g_fingerprintChecks = true;

void SetFingerprintChecks(bool enabled) { g_fingerprintChecks = enabled; }

static uintptr_t g_libBase = 0;

void SetLibraryBase(uintptr_t base) { g_libBase = base; }
uintptr_t LibraryBase() { return g_libBase; }

bool MatchesFingerprint(uintptr_t addr, const Fingerprint& fp)
{
    if(!addr) return false;
    if(!g_fingerprintChecks) return true;
    const uint8_t* code = (const uint8_t*)(addr & ~(uintptr_t)1);     // drop the Thumb bit
    return std::memcmp(code, fp.bytes, sizeof(fp.bytes)) == 0;
}

bool LooksLikeInlineHook(const uint8_t* c, bool thumb)
{
    if(thumb)
    {
        auto hw = [c](int i) -> uint16_t { return (uint16_t)(c[i * 2] | (c[i * 2 + 1] << 8)); };

        int i = (hw(0) == 0xBF00)? 1 : 0;                           // a nop may precede the jump (alignment)
        uint16_t a = hw(i), b = hw(i + 1);

        if((a == 0xF8DF || a == 0xF85F) && (b & 0xF000) == 0xF000) return true;     // ldr.w pc, [pc, #imm]  (Substrate, AML)
        if((a & 0xF800) == 0xF000 && (b & 0xD000) == 0x9000)       return true;     // b.w
        if(a == 0x4778)                                             return true;     // bx pc: continues in ARM state
        return false;
    }

    uint32_t w = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
    if((w & 0xFFFFF000u) == 0xE51FF000u) return true;               // ldr pc, [pc, #-imm]
    if((w & 0xFF000000u) == 0xEA000000u) return true;               // b
    return false;
}

static std::string Hex(const uint8_t* p, size_t n)
{
    std::string out;
    char buf[4];
    for(size_t i = 0; i < n; ++i) { std::snprintf(buf, sizeof(buf), "%02X", p[i]); if(i) out += ' '; out += buf; }
    return out;
}

CodeCheck CheckCode(uintptr_t addr, const Fingerprint& fp, std::string* found)
{
    const uint8_t* code = (const uint8_t*)(addr & ~(uintptr_t)1);
    if(found) *found = Hex(code, 12);

    if(!g_fingerprintChecks) return CodeCheck::Match;
    if(std::memcmp(code, fp.bytes, sizeof(fp.bytes)) == 0) return CodeCheck::Match;
    return LooksLikeInlineHook(code, (addr & 1) != 0)? CodeCheck::LooksHooked : CodeCheck::Different;
}

bool AcceptHookTarget(uintptr_t addr, const Fingerprint& fp, bool strict)
{
    if(!addr) return false;

    std::string found;
    CodeCheck c = CheckCode(addr, fp, &found);

    char where[48] = "";
    if(g_libBase && addr >= g_libBase) std::snprintf(where, sizeof(where), "libGTASA.so+0x%x", (unsigned)((addr & ~(uintptr_t)1) - g_libBase));

    std::string expected = Hex(fp.bytes, sizeof(fp.bytes));

    switch(c)
    {
        case CodeCheck::Match:
            MLA_DBG("%s (%s): code is the one that was analysed", fp.name, where);
            return true;

        case CodeCheck::LooksHooked:
            MLA_LOG("%s (%s) was already hooked by someone else (starts with: %s), hooking on top of it", fp.name, where, found.c_str());
            return true;

        case CodeCheck::Different:
            if(strict)
            {
                MLA_ERR("%s (%s): unexpected code, refusing to hook it (StrictChecks = true). found: %s  expected: %s",
                        fp.name, where, found.c_str(), expected.c_str());
                return false;
            }
            MLA_ERR("%s (%s): unexpected code, this build of the game is not the one the port was analysed with. Hooking anyway "
                    "(set StrictChecks = true in config.ini to refuse). found: %s  expected: %s",
                    fp.name, where, found.c_str(), expected.c_str());
            return true;
    }
    return false;
}

}} // namespace mla::gtasa
