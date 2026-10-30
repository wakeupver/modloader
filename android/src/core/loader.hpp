/*
 *  Mod Loader - Android port
 *      The loader core. It does what the original core does (see src/core/loader.cpp, modinfo.cpp, plugins.cpp):
 *          - finds the mods in the "modloader/" folder and sorts them by priority;
 *          - walks the files of every mod, honouring the profile in modloader.ini;
 *          - asks the plugins what they want to do with each file (GetBehaviour) and hands the file over (InstallFile).
 *
 *      Plugins speak the very same C interface of the original (include/modloader/modloader.h), but here they are
 *      built into the AML plugin instead of being DLLs in "modloader/.data/plugins".
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include "../compat.hpp"
#include <modloader/modloader.h>        // the C plugin interface of the original source tree
#include "config.hpp"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace mla {

struct Mod
{
    modloader_mod_t     c;              // C interface view (id/priority)
    std::string         name;           // folder name
    std::string         path;           // absolute path of the mod folder (without trailing slash)
    std::string         gamePath;       // "modloader/<name>/", relative to the game root (this is what modloader_file_t::buffer starts with)
};

struct ModFile
{
    modloader_file_t    c;              // C interface view
    std::string         buffer;         // c.buffer points here: "modloader/<mod>/<path inside the mod>"
    std::string         abs;            // absolute path on disk
    std::string         rel;            // path inside the mod folder ('/' separators, original case)
    Mod*                mod = nullptr;
    int                 handler = -1;   // index of the plugin that owns the file (MODLOADER_BEHAVIOUR_YES), -1 if none
    std::vector<int>    callme;         // plugins that asked to receive the file (MODLOADER_BEHAVIOUR_CALLME)
    bool                installed = false;
};

class Loader
{
    public:
        struct Paths
        {
            std::string root;           // folder where the game keeps its loose files ("…/Android/data/<package>/files"), no trailing slash
            std::string modloader;      // <root>/modloader
            std::string data;           // <root>/modloader/.data
            std::string cache;          // <root>/modloader/.data/cache
        };

        static Loader& Get();

        /* Creates the folders and default ini files, loads the configs and opens the log. */
        bool Init(const std::string& gameRoot);

        /* Plugins have to be registered before Start. The pointer must stay valid until Shutdown. */
        void RegisterPlugin(modloader_plugin_t* plugin);

        /* OnStartup on the plugins, scan the mods, install the files, Update on the plugins. */
        void Start();
        void Shutdown();

        /* The game reached a state (set by the AML entry point) */
        void SetGameStarted(bool v) { iface.has_game_started = v? 1 : 0; }
        void SetGameLoaded(bool v)  { iface.has_game_loaded  = v? 1 : 0; }

        const Paths&        GetPaths()  const { return paths; }
        const BaseConfig&   Config()    const { return baseCfg; }
        const FolderConfig& Folder()    const { return folderCfg; }
        modloader_t*        CInterface()      { return &iface; }

        const std::vector<std::unique_ptr<Mod>>&     Mods()  const { return mods; }
        const std::vector<std::unique_ptr<ModFile>>& Files() const { return files; }

        /* Exposed for the unit tests (Start() just calls these in order) */
        void ScanMods();
        void DispatchFiles();
        void ResetForTests();

    private:
        Loader();

        struct PluginRec
        {
            modloader_plugin_t* p       = nullptr;
            bool                started = false;
            size_t              order   = 0;
        };

        void ScanModFolder(Mod& mod, const std::string& relDir);
        void LogModSummary() const;
        int  FindHandler(ModFile& file);

        Paths                                   paths;
        BaseConfig                              baseCfg;
        FolderConfig                            folderCfg;
        modloader_t                             iface;
        std::string                             gamepathStr;
        std::vector<PluginRec>                  plugins;        // sorted by priority (highest first)
        std::vector<std::unique_ptr<Mod>>       mods;           // sorted by priority (lowest first, the last one wins)
        std::vector<std::unique_ptr<ModFile>>   files;
        bool                                    initialized = false;
        bool                                    started = false;
};

} // namespace mla
