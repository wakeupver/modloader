/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "loader.hpp"
#include "../util/strutil.hpp"
#include "../util/fs.hpp"
#include "../util/log.hpp"

#include <modloader/util/hash.hpp>      // from the original source tree: filename hash stored in modloader_file_t

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <map>
#include <mutex>

namespace mla {

/*
 *  The "modloader_t" interface handed to the plugins
 */
static void CLog(const char* msg, ...)
{
    va_list va; va_start(va, msg);
    Log::Get().WriteV(LOG_INFO, msg, va);
    va_end(va);
}

static void CvLog(const char* msg, va_list va)
{
    Log::Get().WriteV(LOG_INFO, msg, va);
}

static void CError(const char* msg, ...)
{
    // The original shows a message box here, on Android we just log it (a toast would need the JNI)
    va_list va; va_start(va, msg);
    Log::Get().WriteV(LOG_ERROR, msg, va);
    va_end(va);
}

static std::mutex                                   g_shMutex;
static std::map<std::string, modloader_shdata_t*>   g_shData;

static modloader_shdata_t* CCreateSharedData(const char* name)
{
    if(!name || !*name) return nullptr;
    std::lock_guard<std::mutex> lock(g_shMutex);
    std::string key = ToLower(name);
    if(g_shData.count(key)) return nullptr;
    auto* d = new modloader_shdata_t();
    std::memset(d, 0, sizeof(*d));
    d->type = MODLOADER_SHDATA_EMPTY;
    g_shData[key] = d;
    return d;
}

static void CDeleteSharedData(modloader_shdata_t* data)
{
    std::lock_guard<std::mutex> lock(g_shMutex);
    for(auto it = g_shData.begin(); it != g_shData.end(); ++it)
        if(it->second == data) { delete data; g_shData.erase(it); return; }
}

static modloader_shdata_t* CFindSharedData(const char* name)
{
    if(!name) return nullptr;
    std::lock_guard<std::mutex> lock(g_shMutex);
    auto it = g_shData.find(ToLower(name));
    return (it == g_shData.end())? nullptr : it->second;
}

/*
 *  Loader
 */
Loader::Loader()
{
    std::memset(&iface, 0, sizeof(iface));
    iface.Log              = &CLog;
    iface.vLog             = &CvLog;
    iface.Error            = &CError;
    iface.CreateSharedData = &CCreateSharedData;
    iface.DeleteSharedData = &CDeleteSharedData;
    iface.FindSharedData   = &CFindSharedData;
}

Loader& Loader::Get()
{
    static Loader instance;
    return instance;
}

bool Loader::Init(const std::string& gameRoot)
{
    std::string root = gameRoot;
    while(root.size() > 1 && root.back() == '/') root.pop_back();

    paths.root      = root;
    paths.modloader = root + "/modloader";
    paths.data      = paths.modloader + "/.data";
    paths.cache     = paths.data + "/cache";

    if(!fs::MakeDirs(paths.cache))
    {
        // Nothing sensible can be done without a place to live in, the caller logs this one to logcat
        return false;
    }

    // Base config
    std::string cfgPath = paths.data + "/config.ini";
    if(!fs::Exists(cfgPath))
        fs::WriteFileAtomic(cfgPath, DefaultBaseConfigText());
    baseCfg = BaseConfig();
    bool cfgRead = LoadBaseConfig(cfgPath, baseCfg);

    Log::Get().Open(paths.modloader + "/modloader.log", baseCfg.enableLog, baseCfg.maxLogSize, baseCfg.immediateFlushLog);

    // Folder config
    std::string iniPath = paths.modloader + "/modloader.ini";
    if(!fs::Exists(iniPath))
        fs::WriteFileAtomic(iniPath, DefaultFolderConfigText());
    folderCfg = FolderConfig();
    if(!LoadFolderConfig(iniPath, folderCfg))
        MLA_ERR("Could not read \"%s\", using the default configuration", iniPath.c_str());

    gamepathStr        = root + "/";
    iface.gamepath     = gamepathStr.c_str();
    iface.commonappdata = iface.localappdata = paths.data.c_str();

    MLA_LOG("Mod Loader %d.%d.%d (Android port)", MODLOADER_VERSION_MAJOR, MODLOADER_VERSION_MINOR, MODLOADER_VERSION_REVISION);
    MLA_LOG("Game folder:   %s", paths.root.c_str());
    MLA_LOG("Profile:       %s", folderCfg.profile.c_str());
    MLA_LOG("config.ini (%s): EnablePlugins=%d EnableData=%d EnableStream=%d InjectIdeIpl=%d StrictChecks=%d TraceOpens=%d",
            cfgRead? "read" : "not readable, defaults", (int)baseCfg.enablePlugins, (int)baseCfg.enableData, (int)baseCfg.enableStream,
            (int)baseCfg.injectIdeIpl, (int)baseCfg.strictChecks, (int)baseCfg.traceOpens);

    initialized = true;
    return true;
}

void Loader::ResetForTests()
{
    plugins.clear();
    mods.clear();
    files.clear();
    initialized = false;
    started = false;
}

void Loader::RegisterPlugin(modloader_plugin_t* plugin)
{
    if(!plugin) return;
    PluginRec rec;
    rec.p = plugin;
    rec.order = plugins.size();
    plugins.push_back(rec);
}

void Loader::Start()
{
    if(!initialized || started) return;
    started = true;

    if(!baseCfg.enablePlugins)
    {
        MLA_LOG("Plugins are disabled in config.ini (EnablePlugins = false), nothing will be loaded");
        return;
    }

    // Highest priority first, registration order breaks the ties
    std::stable_sort(plugins.begin(), plugins.end(), [](const PluginRec& a, const PluginRec& b) {
        return a.p->priority > b.p->priority;
    });

    for(auto& rec : plugins)
    {
        modloader_plugin_t* p = rec.p;
        p->major = MODLOADER_VERSION_MAJOR;
        p->minor = MODLOADER_VERSION_MINOR;
        p->revision = MODLOADER_VERSION_REVISION;
        p->loader = &iface;
        p->has_started = 0;

        if(p->priority <= 0)
        {
            MLA_LOG("Plugin \"%s\" is disabled (priority 0)", p->name? p->name : "?");
            continue;
        }

        int rc = p->OnStartup? p->OnStartup(p) : 0;
        if(rc != 0)
        {
            MLA_ERR("Plugin \"%s\" failed to start up", p->name? p->name : "?");
            continue;
        }

        rec.started = true;
        p->has_started = 1;
        MLA_LOG("Plugin \"%s\" %s started", p->name? p->name : "?", p->version? p->version : "");
    }

    ScanMods();
    DispatchFiles();
    LogModSummary();

    for(auto& rec : plugins)
        if(rec.started && rec.p->Update)
            rec.p->Update(rec.p);

    size_t installed = 0;
    for(auto& f : files) if(f->installed) ++installed;
    MLA_LOG("Done: %u mod(s), %u file(s), %u handled by plugins", (unsigned)mods.size(), (unsigned)files.size(), (unsigned)installed);
}

/*
 *  One line per mod with the number of files of each type, and what was decided for the first files of it.
 *  When a mod "does nothing" this is the first thing to look at.
 */
void Loader::LogModSummary() const
{
    for(auto& m : mods)
    {
        std::map<std::string, unsigned> byExt;
        unsigned total = 0;
        for(auto& f : files)
            if(f->mod == m.get()) { ++total; ++byExt[Extension(f->rel).empty()? "(none)" : Extension(f->rel)]; }

        std::string types;
        for(auto& kv : byExt) { char buf[64]; std::snprintf(buf, sizeof(buf), "%s%s=%u", types.empty()? "" : ", ", kv.first.c_str(), kv.second); types += buf; }
        MLA_LOG("Mod \"%s\": %u file(s) [%s]", m->name.c_str(), total, types.c_str());

        unsigned shown = 0;
        for(auto& f : files)
        {
            if(f->mod != m.get()) continue;
            if(++shown > 30) { MLA_LOG("    ... and %u more", total - 30); break; }

            std::string who;
            if(f->handler >= 0) who = plugins[f->handler].p->name;
            for(int idx : f->callme) who += std::string(who.empty()? "" : " + ") + plugins[idx].p->name + " (callme)";
            MLA_LOG("    %s  ->  %s", f->rel.c_str(), who.empty()? "no plugin wants this file" : who.c_str());
        }
    }
}

void Loader::Shutdown()
{
    for(auto& rec : plugins)
    {
        if(rec.started && rec.p->OnShutdown)
            rec.p->OnShutdown(rec.p);
        rec.started = false;
    }
}

/*
 *  Mod scanning
 */
void Loader::ScanMods()
{
    mods.clear();
    files.clear();

    if(folderCfg.active.ignoreAllMods == 1)
    {
        MLA_LOG("All mods are being ignored by the profile \"%s\"", folderCfg.profile.c_str());
        return;
    }

    std::vector<std::pair<std::string, bool>> entries;
    if(!fs::ListDir(paths.modloader, entries))
    {
        MLA_ERR("Could not list \"%s\"", paths.modloader.c_str());
        return;
    }

    for(auto& e : entries)
    {
        if(!e.second) continue;                         // only folders are mods
        if(e.first[0] == '.') continue;                 // ".data", ".profiles" and friends

        if(folderCfg.IsModIgnored(e.first))
        {
            MLA_LOG("Ignoring mod \"%s\"", e.first.c_str());
            continue;
        }

        int prio = folderCfg.GetModPriority(e.first);
        if(prio <= 0)
        {
            MLA_LOG("Ignoring mod \"%s\" (priority 0)", e.first.c_str());
            continue;
        }

        std::unique_ptr<Mod> m(new Mod());
        m->name     = e.first;
        m->path     = paths.modloader + "/" + e.first;
        m->gamePath = "modloader/" + e.first + "/";
        m->c.priority = (uint32_t)prio;
        m->c._pad1  = 0;
        mods.push_back(std::move(m));
    }

    // Lowest priority first, so the files of the highest priority mods are installed last (and win). Ties are
    // broken by name, the mod that comes last in alphabetical order wins.
    std::sort(mods.begin(), mods.end(), [](const std::unique_ptr<Mod>& a, const std::unique_ptr<Mod>& b) {
        if(a->c.priority != b->c.priority) return a->c.priority < b->c.priority;
        return ToLower(a->name) < ToLower(b->name);
    });

    uint64_t id = 0;
    for(auto& m : mods)
    {
        m->c.id = ++id;
        MLA_LOG("Scanning mod \"%s\" (priority %u)", m->name.c_str(), (unsigned)m->c.priority);
        ScanModFolder(*m, "");
    }
}

void Loader::ScanModFolder(Mod& mod, const std::string& relDir)
{
    std::vector<std::pair<std::string, bool>> entries;
    std::string dir = relDir.empty()? mod.path : (mod.path + "/" + relDir);
    if(!fs::ListDir(dir, entries))
        return;

    std::sort(entries.begin(), entries.end());          // deterministic order

    for(auto& e : entries)
    {
        if(e.first[0] == '.') continue;                 // hidden files and folders (.git, .DS_Store, ._foo...)

        std::string rel = relDir.empty()? e.first : (relDir + "/" + e.first);

        if(e.second)
        {
            ScanModFolder(mod, rel);
            continue;
        }

        if(folderCfg.IsFileIgnored(e.first, rel))
            continue;

        std::unique_ptr<ModFile> f(new ModFile());
        f->mod    = &mod;
        f->rel    = rel;
        f->abs    = mod.path + "/" + rel;
        f->buffer = mod.gamePath + rel;

        // modloader_file_t keeps the string positions in 8 bits, the same limit applies to the original
        if(f->buffer.size() > 255)
        {
            MLA_ERR("Ignoring \"%s\": path too long (%u > 255)", f->buffer.c_str(), (unsigned)f->buffer.size());
            continue;
        }

        fs::Stat st = fs::GetStat(f->abs);
        std::memset(&f->c, 0, sizeof(f->c));

        size_t nameOff = f->buffer.find_last_of('/') + 1;
        size_t dot     = f->buffer.find_last_of('.');

        f->c.flags        = 0;
        f->c.buffer       = f->buffer.c_str();
        f->c.pos_eos      = (uint8_t)f->buffer.size();
        f->c.pos_filedir  = (uint8_t)mod.gamePath.size();
        f->c.pos_filename = (uint8_t)nameOff;
        f->c.pos_filext   = (uint8_t)((dot != std::string::npos && dot >= nameOff)? dot + 1 : f->buffer.size());
        f->c.hash         = (uint32_t)modloader::hash(f->buffer.c_str() + nameOff);
        f->c._rsv1        = 0;
        f->c.parent       = &mod.c;
        f->c.size         = st.size;
        f->c.time         = (uint64_t)st.mtime;
        f->c.behaviour    = (uint64_t)-1;

        files.push_back(std::move(f));
    }
}

/*
 *  File dispatching (the same protocol as the original, see GetBehaviour in modloader.h)
 */
static bool ExtMatches(const modloader_plugin_t* p, const std::string& ext)
{
    if(!p->extable || p->extable_len == 0)
        return true;

    for(size_t i = 0; i < p->extable_len; ++i)
    {
        const char* e = p->extable[i];
        if(!e) continue;
        if(e[0] == '*' || strcasecmp(e, ext.c_str()) == 0)
            return true;
    }
    return false;
}

int Loader::FindHandler(ModFile& file)
{
    file.handler = -1;
    file.callme.clear();

    std::string ext = Extension(file.rel);

    for(size_t i = 0; i < plugins.size(); ++i)
    {
        PluginRec& rec = plugins[i];
        if(!rec.started || !rec.p->GetBehaviour) continue;
        if(!ExtMatches(rec.p, ext)) continue;

        int b = rec.p->GetBehaviour(rec.p, &file.c);
        if(b == MODLOADER_BEHAVIOUR_YES)
        {
            file.handler = (int)i;
            break;
        }
        else if(b == MODLOADER_BEHAVIOUR_CALLME)
        {
            file.callme.push_back((int)i);
        }
    }
    return file.handler;
}

void Loader::DispatchFiles()
{
    for(auto& fp : files)
    {
        ModFile& f = *fp;
        FindHandler(f);
        if(f.handler < 0 && f.callme.empty())
            continue;

        bool ok = true;
        if(f.handler >= 0)
        {
            modloader_plugin_t* p = plugins[f.handler].p;
            if(p->InstallFile && p->InstallFile(p, &f.c) != 0)
            {
                MLA_ERR("Plugin \"%s\" failed to install \"%s\"", p->name, f.buffer.c_str());
                ok = false;
            }
        }

        for(int idx : f.callme)
        {
            modloader_plugin_t* p = plugins[idx].p;
            if(p->InstallFile && p->InstallFile(p, &f.c) != 0)
                MLA_ERR("Plugin \"%s\" failed to install \"%s\"", p->name, f.buffer.c_str());
        }

        f.installed = ok;
    }
}

} // namespace mla
