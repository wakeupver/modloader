/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "config.hpp"
#include "../util/strutil.hpp"

#include <ini_parser/ini_parser.hpp>      // from the original source tree (src/shared)

#include <algorithm>
#include <cstdlib>
#include <set>

namespace mla {

typedef std::map<std::string, std::string> KeyMap;

static KeyMap LowerKeys(const KeyMap& in)
{
    KeyMap out;
    for(auto& kv : in) out[ToLower(Trim(kv.first))] = Trim(kv.second);
    return out;
}

static bool ParseBool(const std::string& s, bool def)
{
    std::string v = ToLower(Trim(s));
    if(v == "true" || v == "1" || v == "yes" || v == "on")  return true;
    if(v == "false" || v == "0" || v == "no" || v == "off") return false;
    return def;
}

static bool Has(const KeyMap& m, const char* key) { return m.find(key) != m.end(); }

static bool GetBool(const KeyMap& m, const char* key, bool def)
{
    auto it = m.find(key);
    return (it == m.end())? def : ParseBool(it->second, def);
}

static uint64_t GetU64(const KeyMap& m, const char* key, uint64_t def)
{
    auto it = m.find(key);
    if(it == m.end() || it->second.empty()) return def;
    char* end = nullptr;
    unsigned long long v = std::strtoull(it->second.c_str(), &end, 0);
    return (end && end != it->second.c_str())? (uint64_t)v : def;
}

/*
 *  Base config
 */
bool LoadBaseConfig(const std::string& path, BaseConfig& cfg)
{
    linb::ini ini;
    if(!ini.read_file(path.c_str()))
        return false;

    for(auto& sect : ini)
    {
        std::string name = ToLower(Trim(sect.first));
        KeyMap keys = LowerKeys(sect.second);

        if(name == "config")
        {
            cfg.enablePlugins     = GetBool(keys, "enableplugins",     cfg.enablePlugins);
            cfg.enableLog         = GetBool(keys, "enablelog",         cfg.enableLog);
            cfg.immediateFlushLog = GetBool(keys, "immediateflushlog", cfg.immediateFlushLog);
            cfg.maxLogSize        = GetU64 (keys, "maxlogsize",        cfg.maxLogSize);
        }
        else if(name == "android")
        {
            cfg.enableData   = GetBool(keys, "enabledata",   cfg.enableData);
            cfg.enableStream = GetBool(keys, "enablestream", cfg.enableStream);
            cfg.injectIdeIpl = GetBool(keys, "injectideipl", cfg.injectIdeIpl);
            cfg.logRedirects = GetBool(keys, "logredirects", cfg.logRedirects);
            cfg.strictChecks = GetBool(keys, "strictchecks", cfg.strictChecks);
            cfg.traceOpens   = GetBool(keys, "traceopens",   cfg.traceOpens);
        }
    }
    return true;
}

std::string DefaultBaseConfigText()
{
    return
";\n"
";           Mod Loader Basic Config File (Android)\n"
";\n"
"[Config]\n"
"EnablePlugins     = true        ; Enables/disables the built-in plugins (std.data, std.stream), they are the ones responsible for loading stuff...\n"
"EnableLog         = true        ; Enables/disables modloader/modloader.log. Logging is useful to find bugs or know what's going on\n"
"ImmediateFlushLog = true        ; Flush the log file after every line (slower but the log survives a crash)\n"
"MaxLogSize        = 5242880     ; Maximum size of the modloader.log file in bytes\n"
"\n"
"[Android]\n"
"EnableData        = true        ; std.data:   replace/override any file the game reads from its data folder, plus IDE/IPL injection\n"
"EnableStream      = true        ; std.stream: replace models, textures, collisions, animations... that live inside the IMG archives\n"
"InjectIdeIpl      = true        ; Add the .ide/.ipl files found in the mods to data/gta.dat automatically\n"
"LogRedirects      = true        ; Log (once) every file that is served from a mod instead of from the game\n"
"StrictChecks      = false       ; true = refuse to hook functions whose code differs from the analysed build (false = warn and hook anyway)\n"
"TraceOpens        = true        ; Log the first 600 different files the game opens and which of them are served from a mod (to find out why a mod is not applied)\n";
}

/*
 *  Folder config (modloader.ini)
 */
static void AddList(std::vector<std::string>& out, const KeyMap& raw_unused, const std::map<std::string, std::string>& sect)
{
    (void)raw_unused;
    for(auto& kv : sect)
    {
        std::string k = Trim(kv.first);
        if(!k.empty()) out.push_back(k);
    }
}

static std::vector<std::string> SplitList(const std::string& s)
{
    std::vector<std::string> out;
    size_t b = 0;
    while(b <= s.size())
    {
        size_t e = s.find(',', b);
        if(e == std::string::npos) e = s.size();
        std::string tok = Trim(s.substr(b, e - b));
        if(!tok.empty()) out.push_back(tok);
        b = e + 1;
    }
    return out;
}

bool LoadFolderConfig(const std::string& path, FolderConfig& cfg)
{
    linb::ini ini;
    bool ok = ini.read_file(path.c_str());

    if(ok)
    {
        for(auto& sect : ini)
        {
            std::string name  = Trim(sect.first);
            std::string lname = ToLower(name);

            if(lname == "folder.config")
            {
                KeyMap keys = LowerKeys(sect.second);
                if(Has(keys, "profile") && !keys["profile"].empty()) cfg.profile = keys["profile"];
                cfg.priorityLimit = (int)GetU64(keys, "prioritylimit", (uint64_t)cfg.priorityLimit);
                if(cfg.priorityLimit < 1) cfg.priorityLimit = 1;
                continue;
            }

            // Profiles.<name>.<kind>   (the profile name may contain dots, the kind never does)
            if(lname.compare(0, 9, "profiles.") != 0) continue;
            size_t last = name.rfind('.');
            if(last == std::string::npos || last <= 8) continue;

            std::string pname = name.substr(9, last - 9);
            std::string kind  = ToLower(name.substr(last + 1));
            if(pname.empty()) continue;

            Profile& prof = cfg.profiles[ToLower(pname)];
            if(prof.name.empty()) prof.name = pname;

            if(kind == "config")
            {
                KeyMap keys = LowerKeys(sect.second);
                if(Has(keys, "ignoreallmods"))  prof.ignoreAllMods  = GetBool(keys, "ignoreallmods", false)? 1 : 0;
                if(Has(keys, "ignoreallfiles")) prof.ignoreAllMods  = GetBool(keys, "ignoreallfiles", false)? 1 : 0;
                if(Has(keys, "excludeallmods")) prof.excludeAllMods = GetBool(keys, "excludeallmods", false)? 1 : 0;
                if(Has(keys, "parents"))        prof.parents        = SplitList(keys["parents"]);
            }
            else if(kind == "priority")
            {
                KeyMap keys = LowerKeys(sect.second);
                for(auto& kv : keys)
                {
                    char* end = nullptr;
                    long v = std::strtol(kv.second.c_str(), &end, 10);
                    if(end && end != kv.second.c_str()) prof.priority[kv.first] = (int)v;
                }
            }
            else if(kind == "ignorefiles")   AddList(prof.ignoreFiles,   KeyMap(), sect.second);
            else if(kind == "ignoremods")    AddList(prof.ignoreMods,    KeyMap(), sect.second);
            else if(kind == "includemods")   AddList(prof.includeMods,   KeyMap(), sect.second);
            else if(kind == "exclusivemods") AddList(prof.exclusiveMods, KeyMap(), sect.second);
        }
    }

    cfg.Resolve();
    return ok;
}

/*
 *  Profile inheritance
 */
static void MergeInto(Profile& dst, const Profile& src)
{
    if(src.ignoreAllMods  != -1) dst.ignoreAllMods  = src.ignoreAllMods;
    if(src.excludeAllMods != -1) dst.excludeAllMods = src.excludeAllMods;
    for(auto& kv : src.priority) dst.priority[kv.first] = kv.second;
    dst.ignoreFiles.insert(dst.ignoreFiles.end(),     src.ignoreFiles.begin(),   src.ignoreFiles.end());
    dst.ignoreMods.insert(dst.ignoreMods.end(),       src.ignoreMods.begin(),    src.ignoreMods.end());
    dst.includeMods.insert(dst.includeMods.end(),     src.includeMods.begin(),   src.includeMods.end());
    dst.exclusiveMods.insert(dst.exclusiveMods.end(), src.exclusiveMods.begin(), src.exclusiveMods.end());
}

static void ResolveInto(const FolderConfig& cfg, const std::string& lname, Profile& dst, std::set<std::string>& visiting)
{
    auto it = cfg.profiles.find(lname);
    if(it == cfg.profiles.end() || !visiting.insert(lname).second)
        return;

    const Profile& prof = it->second;

    bool none = false;
    for(auto& p : prof.parents) if(ToLower(p) == "$none") none = true;

    if(!none)
    {
        for(auto& p : prof.parents)
        {
            std::string pl = ToLower(p);
            if(pl == "$current") pl = ToLower(cfg.profile);
            if(pl.empty() || pl == "$none") continue;
            ResolveInto(cfg, pl, dst, visiting);
        }
    }

    MergeInto(dst, prof);
    visiting.erase(lname);
}

void FolderConfig::Resolve()
{
    active = Profile();
    active.name = profile;

    std::set<std::string> visiting;
    ResolveInto(*this, ToLower(profile), active, visiting);
}

bool FolderConfig::IsModIgnored(const std::string& modName) const
{
    if(active.ignoreAllMods == 1)
        return true;

    // Mods listed as exclusive of *another* profile are only loaded by that profile
    for(auto& kv : profiles)
    {
        if(kv.first == ToLower(profile)) continue;
        for(auto& pat : kv.second.exclusiveMods)
            if(WildcardMatch(pat, modName))
                return true;
    }

    for(auto& pat : active.ignoreMods)
        if(WildcardMatch(pat, modName))
            return true;

    if(active.excludeAllMods == 1)
    {
        for(auto& pat : active.includeMods)
            if(WildcardMatch(pat, modName))
                return false;
        return true;
    }
    return false;
}

bool FolderConfig::IsFileIgnored(const std::string& fileName, const std::string& pathInMod) const
{
    for(auto& pat : active.ignoreFiles)
        if(WildcardMatch(pat, fileName) || WildcardMatch(pat, pathInMod))
            return true;
    return false;
}

int FolderConfig::GetModPriority(const std::string& modName) const
{
    auto it = active.priority.find(ToLower(modName));
    int prio = (it == active.priority.end())? 50 : it->second;
    if(prio > priorityLimit) prio = priorityLimit;
    return prio;      // 0 (or less) means "ignore this mod", the loader handles that
}

std::string DefaultFolderConfigText()
{
    return
";\n"
";           Mod Loader Folder Config File\n"
";\n"
"[Folder.Config]\n"
"Profile = Default                 ; Profile to be used to configure mods\n"
"PriorityLimit = 100               ; Max allowed value in Profiles.*.Priority (minimum is 1)\n"
"\n"
"[Profiles.Default.Config]\n"
"IgnoreAllMods  = false            ; Ignores all mods. This essentially disables mods loading.\n"
"ExcludeAllMods = false            ; Excludes all mods from being loaded except the ones at IncludeMods list\n"
"\n"
"[Profiles.Default.Priority]\n"
"; Defines mods priority (which mods should go into effect if both replaces the same file)\n"
"; The priority should be between 1 and PriorityLimit, where higher values override lower ones.\n"
"; Additionally priority 0 makes the mod be ignored (just like IgnoreMods).\n"
"; The default priority is 50.\n"
";MyMod=50\n"
"\n"
"[Profiles.Default.IgnoreFiles]\n"
"; Put wildcard to files to be ignored here (e.g. cheetah.dff, *.dff, path_in_mod/*.dff)\n"
"\n"
"[Profiles.Default.IgnoreMods]\n"
"; Put wildcard to mods to be ignored here\n"
"_ignore\n"
"\n"
"[Profiles.Default.IncludeMods]\n"
"; Put wildcard to mods to be included in the game even when ExcludeAllMods=true\n"
"\n"
"[Profiles.Default.ExclusiveMods]\n"
"; Put wildcard to mods that should be loaded ONLY on this profile\n";
}

} // namespace mla
