/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "std_stream.hpp"
#include "std_data.hpp"
#include "../core/loader.hpp"
#include "../game/host.hpp"
#include "../game/gtasa200.hpp"
#include "../util/strutil.hpp"
#include "../util/fs.hpp"
#include "../util/log.hpp"

#include <cstring>

namespace mla {

static void Hook_LoadCdDirectory(const char* path, int imageIndex)
{
    StdStream& s = StdStream::Get();
    if(s.Patcher().IsEnabled()) s.Patcher().OnLoadCdDirectory(path, imageIndex);
    else                        s.CallOriginalLoadCdDirectory(path, imageIndex);
}

StdStream::StdStream()
{
    static const char* const author  = "LINK/2012 (Android port)";
    static const char* const version = "0.1";
    static const char* exts[] = { "dff", "txd", "col", "ifp", "rrr", "scm", "ipl" };

    std::memset(&plugin, 0, sizeof(plugin));
    plugin.name          = "std.stream";
    plugin.author        = author;
    plugin.version       = version;
    plugin.userdata      = this;
    plugin.extable       = exts;
    plugin.extable_len   = sizeof(exts) / sizeof(exts[0]);
    plugin.priority      = 50;
    plugin.OnStartup     = &CbOnStartup;
    plugin.OnShutdown    = &CbOnShutdown;
    plugin.GetBehaviour  = &CbGetBehaviour;
    plugin.InstallFile   = &CbInstallFile;
    plugin.ReinstallFile = &CbReinstallFile;
    plugin.UninstallFile = &CbUninstallFile;
    plugin.Update        = &CbUpdate;
}

StdStream& StdStream::Get()
{
    static StdStream instance;
    return instance;
}

void StdStream::Reset()
{
    std::lock_guard<std::mutex> lock(mtx);
    sources.clear();
    layout = ImgLayout();
    origLoad = nullptr;
    patcher.Configure(StreamPatcher::Api(), nullptr, "");     // disabled
}

void StdStream::CallOriginalLoadCdDirectory(const char* path, int idx)
{
    if(origLoad) ((gtasa::fnLoadCdDirectory)origLoad)(path, idx);
}

/*
 *  Plugin callbacks
 */
int StdStream::CbGetBehaviour(modloader_plugin_t* p, modloader_file_t* file)
{
    if(!Loader::Get().Config().enableStream)
        return MODLOADER_BEHAVIOUR_NO;

    std::string ext = ToLower(file->buffer + file->pos_filext);
    if(ext == "ipl" && !IsBinaryIplFile(std::string(p->loader->gamepath) + file->buffer))
        return MODLOADER_BEHAVIOUR_NO;                  // text IPL, std.data adds it to gta.dat

    std::string name = file->buffer + file->pos_filename;
    if(name.size() > 23)
    {
        MLA_ERR("std.stream: \"%s\" has a name longer than 23 characters, it cannot be an IMG entry", file->buffer);
        return MODLOADER_BEHAVIOUR_NO;
    }

    file->behaviour = file->hash;
    return MODLOADER_BEHAVIOUR_YES;
}

int StdStream::CbInstallFile(modloader_plugin_t* p, const modloader_file_t* file)
{
    StdStream& s = Get();

    Source src;
    src.img.name  = file->buffer + file->pos_filename;
    src.img.abs   = std::string(p->loader->gamepath) + file->buffer;
    src.img.size  = file->size;
    src.img.mtime = (int64_t)file->time;
    src.priority  = file->parent? file->parent->priority : 0;
    src.modId     = file->parent? file->parent->id : 0;

    std::string key = ToLower(src.img.name);

    std::lock_guard<std::mutex> lock(s.mtx);
    auto it = s.sources.find(key);
    if(it == s.sources.end())
        s.sources.emplace(key, src);
    else if(src.priority > it->second.priority || (src.priority == it->second.priority && src.modId >= it->second.modId))
        it->second = src;                               // higher priority mod (or later one on a tie) wins
    return 0;
}

int StdStream::CbReinstallFile(modloader_plugin_t* p, const modloader_file_t* file)
{
    return CbInstallFile(p, file);
}

int StdStream::CbUninstallFile(modloader_plugin_t*, const modloader_file_t*)
{
    return 0;       // no hot reloading on Android
}

int StdStream::CbOnShutdown(modloader_plugin_t*)
{
    return 0;
}

bool StdStream::BuildArchive()
{
    std::vector<ImgSource> list;
    for(auto& kv : sources) list.push_back(kv.second.img);

    std::vector<std::string> skipped;
    layout = LayoutImg(list, &skipped);
    for(auto& s : skipped)
        MLA_ERR("std.stream: skipping %s", s.c_str());

    if(layout.entries.empty())
        return false;

    const Loader::Paths& paths = Loader::Get().GetPaths();
    std::string img = paths.cache + "/stream.img";
    std::string sig = paths.cache + "/stream.sig";

    if(IsImgUpToDate(img, sig, layout))
    {
        MLA_LOG("std.stream: generated archive is up to date (%u entries)", (unsigned)layout.entries.size());
    }
    else
    {
        MLA_LOG("std.stream: building the archive with %u entries (%u KB), this is only done when the files change",
                (unsigned)layout.entries.size(), (unsigned)(layout.totalSize / 1024));

        fs::RemoveFile(sig);
        if(!WriteImg(img, layout))
        {
            MLA_ERR("std.stream: could not write \"%s\" (out of space? file changed while reading?)", img.c_str());
            return false;
        }
        WriteImgSignature(sig, layout);
    }

    StdData::Get().AddAlias(Alias(), img);
    return true;
}

void StdStream::CbUpdate(modloader_plugin_t*)
{
    StdStream& s = Get();
    const Host* host = GetHost();

    std::lock_guard<std::mutex> lock(s.mtx);
    if(s.sources.empty())
    {
        MLA_LOG("std.stream: no models/textures/collisions/animations in the mods");
        return;
    }

    if(!s.BuildArchive())
        return;

    if(!host)
        return;                 // tests

    // ---- hand everything to the patcher ----
    StreamPatcher::Api api;
    api.files      = (gtasa::StreamingFile*)host->Sym(gtasa::sym::ms_files);
    api.infos      = (gtasa::CStreamingInfo*)host->Sym(gtasa::sym::ms_aInfoForModel);
    api.bufferSize = (int*)host->Sym(gtasa::sym::ms_streamingBufferSize);

    gtasa::fnAddImageToList     addImage  = (gtasa::fnAddImageToList)host->Sym(gtasa::sym::AddImageToList);
    gtasa::fnCFileMgr_OpenFile  openFile  = (gtasa::fnCFileMgr_OpenFile)host->Sym(gtasa::sym::CFileMgr_OpenFile);
    gtasa::fnCFileMgr_CloseFile closeFile = (gtasa::fnCFileMgr_CloseFile)host->Sym(gtasa::sym::CFileMgr_CloseFile);
    gtasa::fnOS_FileRead        readFile  = (gtasa::fnOS_FileRead)host->Sym(gtasa::sym::OS_FileRead);

    if(!api.files || !api.infos || !api.bufferSize || !addImage || !openFile || !closeFile || !readFile ||
       !gtasa::MatchesFingerprint((uintptr_t)readFile, gtasa::kFp_OS_FileRead))
    {
        MLA_ERR("std.stream: some of the streaming functions of the game were not found, replacing models/textures is disabled");
        return;
    }

    api.addImageToList = [addImage](const char* path, bool flag) { return addImage(path, flag); };

    api.readDirectory = [openFile, closeFile, readFile](const char* path, std::vector<uint8_t>& dir) -> bool {
        unsigned h = openFile(path, "rb");
        if(!h) return false;

        // the same reads LoadCdDirectory does: "VER2", the entry count, then count * 32 bytes
        uint8_t head[8];
        bool ok = readFile((void*)(uintptr_t)h, head, 8) == 0;
        uint32_t count = ok? (uint32_t)head[4] | ((uint32_t)head[5] << 8) | ((uint32_t)head[6] << 16) | ((uint32_t)head[7] << 24) : 0;
        ok = ok && count > 0 && count < 200000;
        if(ok)
        {
            dir.assign(8 + (size_t)count * 32, 0);
            std::memcpy(dir.data(), head, 8);
            ok = readFile((void*)(uintptr_t)h, dir.data() + 8, (int)(count * 32)) == 0;
        }
        closeFile(h);
        return ok;
    };

    StdStream* self = &s;
    api.originalLoadCdDirectory = [self](const char* path, int idx) { self->CallOriginalLoadCdDirectory(path, idx); };

    s.patcher.Configure(api, &s.layout, Alias());

    if(s.patcher.StreamingAlreadyInitialised())
    {
        MLA_LOG("std.stream: the game had already loaded its IMG directories when the loader started, patching in place");
        s.patcher.PatchLate();
    }
}

int StdStream::CbOnStartup(modloader_plugin_t*)
{
    StdStream& s = Get();
    const Host* host = GetHost();

    if(!host)
    {
        MLA_LOG("std.stream: running without a game (tests)");
        return 0;
    }

    // The generated archive is opened by the game through an alias that only std.data's hook of OS_FileOpen understands
    if(!StdData::Get().IsHooked())
    {
        MLA_ERR("std.stream: needs std.data (OS_FileOpen hook) to be running, it is disabled");
        return 1;
    }

    uintptr_t load = host->Sym(gtasa::sym::LoadCdDirectory);
    if(!load)
    {
        MLA_ERR("std.stream: CStreaming::LoadCdDirectory was not found in libGTASA.so, this version of the game is not supported");
        return 1;
    }
    if(!gtasa::MatchesFingerprint(load, gtasa::kFp_LoadCdDirectory))
    {
        MLA_ERR("std.stream: CStreaming::LoadCdDirectory does not look like the one of GTA SA 2.00, refusing to hook it");
        return 1;
    }

    if(!host->Hook((void*)load, (void*)&Hook_LoadCdDirectory, s.OriginalLoadSlot()) || !s.origLoad)
    {
        MLA_ERR("std.stream: failed to hook CStreaming::LoadCdDirectory");
        s.origLoad = nullptr;
        return 1;
    }

    MLA_LOG("std.stream: CStreaming::LoadCdDirectory hooked");
    return 0;
}

} // namespace mla
