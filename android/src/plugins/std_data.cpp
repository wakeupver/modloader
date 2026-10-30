/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "std_data.hpp"
#include "gtadat.hpp"
#include "../core/loader.hpp"
#include "../game/host.hpp"
#include "../game/gtasa200.hpp"
#include "../util/strutil.hpp"
#include "../util/fs.hpp"
#include "../util/log.hpp"

#include <algorithm>
#include <cstring>
#include <map>

namespace mla {

bool IsStreamExtension(const std::string& ext)
{
    return ext == "dff" || ext == "txd" || ext == "col" || ext == "ifp" || ext == "rrr" || ext == "scm";
}

bool IsBinaryIplFile(const std::string& absPath)
{
    char magic[4] = {0, 0, 0, 0};
    if(fs::ReadPrefix(absPath, magic, 4) != 4) return false;
    return std::memcmp(magic, "bnry", 4) == 0;
}

/*
 *  The hook
 */
static int Hook_OS_FileOpen(int area, void** out, const char* name, int access)
{
    StdData& d = StdData::Get();

    // Only reads of game data. Absolute names are never game files (they are what we hand back ourselves).
    if(name && name[0] != '\0' && name[0] != '/' && area != gtasa::kAreaUser &&
       (access == gtasa::kAccessRead || access == gtasa::kAccessReadAsync))
    {
        std::string abs;
        std::string redirect = d.Redirect(name, &abs);
        d.TraceOpen(area, access, name, abs);
        if(!redirect.empty())
        {
            int rc = d.OriginalOpen(area, out, redirect.c_str(), access);
            if(rc == 0)
                return 0;

            MLA_ERR("std.data: could not open \"%s\" (rc=%d), falling back to the original \"%s\"", redirect.c_str(), rc, name);
        }
    }

    return d.OriginalOpen(area, out, name, access);
}

/*
 *  StdData
 */
StdData::StdData()
{
    static const char* const author  = "LINK/2012 (Android port)";
    static const char* const version = "0.1";

    std::memset(&plugin, 0, sizeof(plugin));
    plugin.name         = "std.data";
    plugin.author       = author;
    plugin.version      = version;
    plugin.userdata     = this;
    plugin.extable      = nullptr;      // all extensions
    plugin.extable_len  = 0;
    plugin.priority     = 60;           // asked before std.stream, so it can receive the stream files as MODLOADER_BEHAVIOUR_CALLME
    plugin.OnStartup    = &CbOnStartup;
    plugin.OnShutdown   = &CbOnShutdown;
    plugin.GetBehaviour = &CbGetBehaviour;
    plugin.InstallFile  = &CbInstallFile;
    plugin.ReinstallFile = &CbReinstallFile;
    plugin.UninstallFile = &CbUninstallFile;
    plugin.Update       = &CbUpdate;
}

StdData& StdData::Get()
{
    static StdData instance;
    return instance;
}

void StdData::Reset()
{
    std::lock_guard<std::recursive_mutex> lock(mtx);
    vfs = Vfs();
    candidates.clear();
    logged.clear();
    traced.clear();
    traceLeft = 0;
    gtaDatPath.clear();
    gtaDatBuilt = false;
    reader = nullptr;
    origOpen = nullptr;
    hooked = false;
}

void StdData::AddAlias(const std::string& aliasPath, const std::string& abs)
{
    std::lock_guard<std::recursive_mutex> lock(mtx);
    vfs.AddAlias(aliasPath, abs);
}

void StdData::SetOriginalReader(OriginalReader r)
{
    std::lock_guard<std::recursive_mutex> lock(mtx);
    reader = std::move(r);
}

int StdData::OriginalOpen(int area, void** out, const char* name, int access)
{
    return origOpen? ((gtasa::fnOS_FileOpen)origOpen)(area, out, name, access) : -1;
}

std::string StdData::MakeGameName(const std::string& abs)
{
    // OS_FileOpen/NvFOpen treat a name that starts with '/' as a path relative to the external storage root, which is the part of
    // StorageRootBuffer before the first "/Android" (strstr, so "/Android_unprotected/..." is found too, and "/android" as a second
    // try). Our files are under that same folder, so from there on is what the game wants.
    size_t pos = abs.find("/Android");
    if(pos == std::string::npos) pos = abs.find("/android");
    return (pos == std::string::npos)? std::string() : abs.substr(pos);
}

/*
 *  Resolution
 */
std::string StdData::BuildGtaDat(const char* gameName)
{
    // (mtx is held by the caller)
    if(gtaDatBuilt) return gtaDatPath;
    gtaDatBuilt = true;

    if(candidates.empty())
        return std::string();

    // The game's own gta.dat (or the one of a mod that replaces it)
    auto readBase = [&](const char* name, const char* key, std::string& out) -> bool {
        const VfsFile* f = vfs.Find(key);
        if(f) return fs::ReadFile(f->abs, out);
        return reader && reader(name, out);
    };

    std::string base;
    if(!readBase(gameName, "data/gta.dat", base))
    {
        MLA_ERR("std.data: could not read gta.dat, the IDE/IPL files of the mods will not be loaded");
        return std::string();
    }

    // Files the game already loads: a mod file with the same name is a replacement, not an addition
    std::set<std::string> referenced = GtaDatReferencedNames(base);
    std::string def;
    if(readBase("DATA\\DEFAULT.DAT", "data/default.dat", def))
    {
        std::set<std::string> more = GtaDatReferencedNames(def);
        referenced.insert(more.begin(), more.end());
    }

    // Same name in several mods: only the one with the highest priority is added
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if(a.priority != b.priority) return a.priority < b.priority;
        if(a.modId != b.modId)       return a.modId < b.modId;
        return ToLower(a.rel) < ToLower(b.rel);
    });

    std::map<std::string, size_t> winner;           // "kind:name" -> index in candidates
    for(size_t i = 0; i < candidates.size(); ++i)
        winner[candidates[i].kind + ":" + candidates[i].baseLower] = i;

    std::vector<GtaDatItem> items;
    unsigned seq = 0;
    for(size_t i = 0; i < candidates.size(); ++i)
    {
        const Candidate& c = candidates[i];
        if(winner[c.kind + ":" + c.baseLower] != i) continue;
        if(referenced.count(c.baseLower)) continue;

        char alias[64];
        std::snprintf(alias, sizeof(alias), "modloader/.vfs/m%03u.%s", ++seq, c.kind == "IDE"? "ide" : "ipl");
        vfs.AddAlias(alias, c.abs);

        GtaDatItem item;
        item.kind = c.kind;
        item.path = alias;
        items.push_back(item);

        MLA_LOG("gta.dat: %s %s  <-  %s", c.kind.c_str(), alias, c.rel.c_str());
    }

    if(items.empty())
        return std::string();

    std::string out = Loader::Get().GetPaths().cache + "/gta.dat";
    if(!fs::WriteFileAtomic(out, MergeGtaDat(base, items)))
    {
        MLA_ERR("std.data: could not write \"%s\"", out.c_str());
        return std::string();
    }

    gtaDatPath = out;
    return gtaDatPath;
}

std::string StdData::ResolveAbs(const char* gameName)
{
    std::string key = NormalizeKey(gameName);
    if(key.empty()) return std::string();

    std::lock_guard<std::recursive_mutex> lock(mtx);

    if(key == "data/gta.dat" && Loader::Get().Config().injectIdeIpl)
    {
        std::string p = BuildGtaDat(gameName);
        if(!p.empty()) return p;
    }

    const VfsFile* f = vfs.Find(key);
    return f? f->abs : std::string();
}

void StdData::TraceOpen(int area, int access, const char* gameName, const std::string& abs)
{
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if(traceLeft <= 0) return;
    if(!traced.insert(NormalizeKey(gameName)).second) return;

    if(abs.empty()) MLA_LOG("trace: the game opens \"%s\" (area %d, access %d)", gameName, area, access);
    else            MLA_LOG("trace: the game opens \"%s\" (area %d, access %d)  ->  %s", gameName, area, access, abs.c_str());

    if(--traceLeft == 0)
        MLA_LOG("trace: limit reached, the next files are not listed (TraceOpens in config.ini)");
}

std::string StdData::Redirect(const char* gameName, std::string* absOut)
{
    std::string abs = ResolveAbs(gameName);
    if(absOut) *absOut = abs;
    if(abs.empty())
        return std::string();

    std::string name = MakeGameName(abs);
    std::string key  = NormalizeKey(gameName);

    std::lock_guard<std::recursive_mutex> lock(mtx);
    if(name.empty())
    {
        if(logged.insert("!" + key).second)
            MLA_ERR("std.data: \"%s\" is not under an \"/Android...\" folder of the external storage and cannot be served to the game", abs.c_str());
        return std::string();
    }

    if(Loader::Get().Config().logRedirects && logged.insert(key).second)
        MLA_LOG("std.data: \"%s\" -> %s", gameName, abs.c_str());

    return name;
}

/*
 *  Plugin callbacks
 */
int StdData::CbGetBehaviour(modloader_plugin_t* p, modloader_file_t* file)
{
    if(!Loader::Get().Config().enableData)
    {
        static bool told = false;
        if(!told) { told = true; MLA_ERR("std.data is switched off by config.ini (EnableData = false): the files of the mods are ignored"); }
        return MODLOADER_BEHAVIOUR_NO;
    }

    std::string ext = ToLower(file->buffer + file->pos_filext);
    bool streamLike = IsStreamExtension(ext);
    if(!streamLike && ext == "ipl")
        streamLike = IsBinaryIplFile(std::string(p->loader->gamepath) + file->buffer);

    file->behaviour = file->hash;
    return streamLike? MODLOADER_BEHAVIOUR_CALLME : MODLOADER_BEHAVIOUR_YES;
}

int StdData::CbInstallFile(modloader_plugin_t* p, const modloader_file_t* file)
{
    StdData& d = Get();

    std::string buffer(file->buffer);
    std::string ext = ToLower(buffer.substr(file->pos_filext));

    VfsFile f;
    f.abs      = std::string(p->loader->gamepath) + buffer;
    f.rel      = buffer.substr(file->pos_filedir);
    f.modName  = (file->pos_filedir > 11)? buffer.substr(10, file->pos_filedir - 11) : std::string();
    f.priority = file->parent? file->parent->priority : 0;
    f.modId    = file->parent? file->parent->id : 0;
    f.size     = file->size;

    bool streamLike = IsStreamExtension(ext) || (ext == "ipl" && IsBinaryIplFile(f.abs));

    std::lock_guard<std::recursive_mutex> lock(d.mtx);
    // main.scm is a loose file as well as a kind of IMG entry, so scripts are reachable by name too
    d.vfs.Add(f, /*allowName=*/ !streamLike || ext == "scm");

    if(!streamLike && (ext == "ide" || ext == "ipl") && Loader::Get().Config().injectIdeIpl)
    {
        Candidate c;
        c.kind      = (ext == "ide")? "IDE" : "IPL";
        c.abs       = f.abs;
        c.rel       = f.rel;
        c.baseLower = ToLower(BaseName(f.rel));
        c.priority  = f.priority;
        c.modId     = f.modId;
        d.candidates.push_back(c);
    }
    return 0;
}

int StdData::CbReinstallFile(modloader_plugin_t* p, const modloader_file_t* file)
{
    return CbInstallFile(p, file);
}

int StdData::CbUninstallFile(modloader_plugin_t*, const modloader_file_t*)
{
    // Hot reloading is not supported on Android: mods are scanned once, when the game starts
    return 0;
}

void StdData::CbUpdate(modloader_plugin_t*)
{
    StdData& d = Get();
    std::lock_guard<std::recursive_mutex> lock(d.mtx);
    MLA_LOG("std.data: %u file(s) by path, %u by name, %u IDE/IPL file(s) to add to gta.dat",
            (unsigned)d.vfs.NumExact(), (unsigned)d.vfs.NumByName(), (unsigned)d.candidates.size());
}

int StdData::CbOnShutdown(modloader_plugin_t*)
{
    return 0;
}

int StdData::CbOnStartup(modloader_plugin_t*)
{
    StdData& d = Get();
    const Host* host = GetHost();

    {
        std::lock_guard<std::recursive_mutex> lock(d.mtx);
        d.traceLeft = Loader::Get().Config().traceOpens? 600 : 0;
    }

    if(!host)
    {
        MLA_LOG("std.data: running without a game (tests)");
        return 0;
    }

    uintptr_t open = host->Sym(gtasa::sym::OS_FileOpen);
    if(!open)
    {
        MLA_ERR("std.data: OS_FileOpen was not found in libGTASA.so, this version of the game is not supported");
        return 1;
    }
    if(!gtasa::AcceptHookTarget(open, gtasa::kFp_OS_FileOpen, Loader::Get().Config().strictChecks))
        return 1;

    // Reading the original gta.dat needs two more of the OS_File* functions (optional)
    uintptr_t rd = host->Sym(gtasa::sym::OS_FileRead);
    uintptr_t cl = host->Sym(gtasa::sym::OS_FileClose);

    // The slot that receives the trampoline is the member itself: the hook can be called by another thread the moment it is
    // installed, and by then it must already be able to call the original.
    if(!host->Hook((void*)open, (void*)&Hook_OS_FileOpen, d.OriginalOpenSlot()) || !d.origOpen)
    {
        MLA_ERR("std.data: failed to hook OS_FileOpen");
        d.origOpen = nullptr;
        return 1;
    }
    d.hooked = true;

    if(rd && cl && gtasa::AcceptHookTarget(rd, gtasa::kFp_OS_FileRead, Loader::Get().Config().strictChecks))
    {
        StdData* self = &d;
        d.SetOriginalReader([self, rd, cl](const char* gameName, std::string& out) -> bool {
            void* h = nullptr;
            if(self->OriginalOpen(0, &h, gameName, gtasa::kAccessRead) != 0 || !h)
                return false;

            gtasa::fnOS_FileRead  fnRead  = (gtasa::fnOS_FileRead)rd;
            gtasa::fnOS_FileClose fnClose = (gtasa::fnOS_FileClose)cl;

            // OS_FileRead returns 0 when it read everything that was asked and 2/3 on end of file/error. The files read
            // here are the small text files (gta.dat...) so one byte at a time, like CFileMgr::ReadLine does at the end
            // of a file, is plenty fast and also works for entries stored inside the OBB.
            out.clear();
            char c;
            while(out.size() < 4u * 1024 * 1024 && fnRead(h, &c, 1) == 0)
                out.push_back(c);

            fnClose(h);
            return !out.empty();
        });
    }
    else
        MLA_ERR("std.data: OS_FileRead/OS_FileClose not available, IDE/IPL files of the mods cannot be added to gta.dat");

    MLA_LOG("std.data: OS_FileOpen hooked");
    return 0;
}

} // namespace mla
