/*
 *  Mod Loader - Android port
 *      Where does the code of libGTASA.so really find a global variable?
 *
 *      The library exports its variables (CStreaming::ms_files, CStreaming::ms_aInfoForModel...) and the dynamic linker
 *      stores the address of each one in a slot of the GOT, which is where the code of the library reads it from. dlsym()
 *      gives the exported address, the GOT slot gives the one the code uses. They are the same until something changes the
 *      slot: the Fastman92 plugin loader does that to give the game bigger arrays (more IMG archives, more models...), and
 *      from then on the exported symbol is a dead, empty array. Reading the GOT slot is how this port finds the live one.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include <cstdint>
#include <cstddef>

namespace mla { namespace gtasa {

/* What is needed to walk the dynamic section of a loaded 32 bits library */
struct ImageInfo
{
    uintptr_t   bias = 0;           // address the library is loaded at minus the address it was linked for
    const void* phdr = nullptr;     // its program headers (Elf32_Phdr[]), where the loader keeps them
    unsigned    phnum = 0;
};

/*
 *  Finds a library that is loaded in this process (dl_iterate_phdr) by the end of its path, "libGTASA.so".
 *  Only does something on 32 bits ARM, the only place this port runs.
 */
bool FindLoadedImage(const char* nameSuffix, ImageInfo* out);

/* For images that sit completely in memory with their ELF header at 'base' (the unit tests build one) */
bool ImageFromMemory(uintptr_t base, ImageInfo* out);

/*
 *  Address of the GOT slot that holds the address of 'symbol' (an R_ARM_GLOB_DAT or R_ARM_ABS32 relocation of the dynamic
 *  section), 0 if there is none.
 *  (The program headers of libGTASA.so are NOT where its ELF header says, the patcher that added the Fastman92 sections
 *  moved them to a segment of their own: that is why the loader's own copy is used.)
 */
uintptr_t FindGotSlot(const ImageInfo& image, const char* symbol);

/*
 *  The address the code of the library uses for the data symbol: the current content of its GOT slot, or 'exported' (what dlsym
 *  returned) when there is no usable slot. '*relocated' tells whether the two differ.
 */
uintptr_t ResolveDataSymbol(const ImageInfo& image, const char* symbol, uintptr_t exported, bool* relocated = nullptr);

}} // namespace mla::gtasa
