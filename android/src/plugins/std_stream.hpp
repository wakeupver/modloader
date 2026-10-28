/*
 *  Mod Loader - Android port
 *      std.stream - the Android counterpart of the original "std.stream" plugin: replaces (and adds) the models (.dff),
 *      textures (.txd), collisions (.col), animations (.ifp), streamed maps (binary .ipl), vehicle recordings (.rrr) and
 *      streamed scripts (.scm) that live inside the IMG archives of the game. The file name inside the mod is the name of
 *      the entry it replaces, the rest of its path doesn't matter.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include "../compat.hpp"
#include <modloader/modloader.h>
#include "img.hpp"
#include "stream_patcher.hpp"

#include <map>
#include <mutex>
#include <string>

namespace mla {

class StdStream
{
    public:
        static StdStream& Get();

        modloader_plugin_t* Plugin() { return &plugin; }

        /* The IMG path the game is told to open for the generated archive */
        static const char* Alias() { return "modloader/.vfs/stream.img"; }

        StreamPatcher&  Patcher()  { return patcher; }
        const ImgLayout& Layout() const { return layout; }
        void CallOriginalLoadCdDirectory(const char* path, int idx);
        void SetOriginalLoadCdDirectory(void* fn) { origLoad = fn; }
        void** OriginalLoadSlot() { return &origLoad; }

        /* Builds the generated archive and the layout out of what was installed (Update does this) */
        bool BuildArchive();

        /* Test support */
        void Reset();

        struct Source
        {
            ImgSource   img;
            uint32_t    priority = 0;
            uint64_t    modId = 0;
        };
        const std::map<std::string, Source>& Sources() const { return sources; }

    private:
        StdStream();

        static int  CbGetBehaviour(modloader_plugin_t*, modloader_file_t*);
        static int  CbInstallFile(modloader_plugin_t*, const modloader_file_t*);
        static int  CbReinstallFile(modloader_plugin_t*, const modloader_file_t*);
        static int  CbUninstallFile(modloader_plugin_t*, const modloader_file_t*);
        static int  CbOnStartup(modloader_plugin_t*);
        static int  CbOnShutdown(modloader_plugin_t*);
        static void CbUpdate(modloader_plugin_t*);

        modloader_plugin_t          plugin;
        std::map<std::string, Source> sources;      // lower case entry name -> winner
        ImgLayout                   layout;
        StreamPatcher               patcher;
        void*                       origLoad = nullptr;
        std::mutex                  mtx;
};

} // namespace mla
