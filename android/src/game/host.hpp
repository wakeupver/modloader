/*
 *  Mod Loader - Android port
 *      The only things the built-in plugins need from the AML plugin that hosts them. Keeping this tiny interface
 *      between the two means everything but aml_entry.cpp can be unit tested on a normal PC.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include <cstdint>

namespace mla {

struct Host
{
    /* Inline hooks 'target' so that it jumps to 'replacement'; '*original' receives a callable trampoline. */
    bool        (*Hook)(void* target, void* replacement, void** original);

    /* Address of an exported symbol of libGTASA.so (mangled name), 0 when it does not exist. For Thumb functions the
       returned address has the lowest bit set, exactly like dlsym. */
    uintptr_t   (*Sym)(const char* mangledName);
};

/* Set by the AML entry point (or by a test) before the plugins start. */
const Host* GetHost();
void        SetHost(const Host* host);

} // namespace mla
