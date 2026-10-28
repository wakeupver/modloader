/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "host.hpp"

namespace mla {

static const Host* g_host = nullptr;

const Host* GetHost()               { return g_host; }
void        SetHost(const Host* h)  { g_host = h; }

} // namespace mla
