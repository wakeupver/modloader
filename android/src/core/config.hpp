/*
 *  Mod Loader - Android port
 *      Configuration: "modloader/.data/config.ini" (base config) and "modloader/modloader.ini" (profiles).
 *      Both files keep the exact same syntax and semantics of the original Mod Loader (see doc/Profiles.md),
 *      parsed by the very same ini parser the original source tree ships (src/shared/ini_parser).
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include "../compat.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace mla {

/*
 *  modloader/.data/config.ini
 */
struct BaseConfig
{
    /* ---- [Config], same keys as the original ---- */
    bool     enablePlugins      = true;
    bool     enableLog          = true;
    bool     immediateFlushLog  = true;
    uint64_t maxLogSize         = 5242880;

    /* ---- [Android], new keys ---- */
    bool     enableData         = true;     // std.data   (replace/override game files, IDE/IPL injection)
    bool     enableStream       = true;     // std.stream (replace models/textures/collisions/animations inside the IMG archives)
    bool     injectIdeIpl       = true;     // add the .ide/.ipl files found in mods to gta.dat automatically
    bool     logRedirects       = true;     // log every file served from a mod (once per file)
    bool     strictChecks       = false;    // refuse to hook a function whose code is not the one the port was analysed with
};

/* Returns false when the file could not be read (the defaults stay in 'cfg') */
bool        LoadBaseConfig(const std::string& path, BaseConfig& cfg);
std::string DefaultBaseConfigText();

/*
 *  modloader/modloader.ini
 */
struct Profile
{
    std::string                 name;
    int                         ignoreAllMods  = -1;    // -1 = not set (inherit), 0 = false, 1 = true
    int                         excludeAllMods = -1;
    std::vector<std::string>    parents;
    std::map<std::string, int>  priority;               // lower case mod name -> priority
    std::vector<std::string>    ignoreFiles;
    std::vector<std::string>    ignoreMods;
    std::vector<std::string>    includeMods;
    std::vector<std::string>    exclusiveMods;
};

struct FolderConfig
{
    std::string                         profile = "Default";   // selected profile
    int                                 priorityLimit = 100;
    std::map<std::string, Profile>      profiles;               // lower case profile name -> profile
    Profile                             active;                 // selected profile with inheritance applied (see Resolve)

    void Resolve();

    /* Queries on the active profile */
    bool IsModIgnored(const std::string& modName) const;
    bool IsFileIgnored(const std::string& fileName, const std::string& pathInMod) const;
    int  GetModPriority(const std::string& modName) const;      // default is 50
};

bool        LoadFolderConfig(const std::string& path, FolderConfig& cfg);   // always calls Resolve()
std::string DefaultFolderConfigText();

} // namespace mla
