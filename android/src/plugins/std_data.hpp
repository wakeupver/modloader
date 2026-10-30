/*
 *  Mod Loader - Android port
 *      std.data - the Android counterpart of the original "std.data" plugin.
 *
 *      Every file the game reads goes through OS_FileOpen (CFileMgr::OpenFile, the IMG archives, audio, texture
 *      databases... all of it). This plugin hooks that function and, when a mod provides the file being asked for,
 *      makes the game open the file of the mod instead. Files can be replaced by their path or just by their name,
 *      see vfs.hpp. On top of that, .ide/.ipl files that are not replacements are added to gta.dat, see gtadat.hpp.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include "../compat.hpp"
#include <modloader/modloader.h>
#include "vfs.hpp"

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace mla {

class StdData
{
    public:
        /* Reads a file of the game itself (not of a mod). Returns false on failure. */
        typedef std::function<bool(const char* gameName, std::string& contents)> OriginalReader;

        static StdData& Get();

        modloader_plugin_t* Plugin() { return &plugin; }
        Vfs&                GetVfs() { return vfs; }

        /* Absolute path of the file that should be served for what the game asked for ("" = serve the original). */
        std::string ResolveAbs(const char* gameName);

        /* Name to give to OS_FileOpen so it opens the file above ("" = no redirection possible). */
        std::string Redirect(const char* gameName, std::string* absOut = nullptr);

        /* Logs (until the limit is reached) the different files the game asks for, and the ones that come from a mod */
        void TraceOpen(int area, int access, const char* gameName, const std::string& abs);

        /* "/storage/emulated/0/Android/data/<pkg>/files/x" -> "/Android/data/<pkg>/files/x", see NvFOpen in libGTASA.so */
        static std::string MakeGameName(const std::string& abs);

        void SetOriginalReader(OriginalReader reader);

        /* Thread safe way of adding an alias (std.stream uses it for its generated IMG) */
        void AddAlias(const std::string& aliasPath, const std::string& abs);

        /* Called by the hook of OS_FileOpen */
        int  OriginalOpen(int area, void** out, const char* name, int access);
        void SetOriginalOpen(void* fn) { origOpen = fn; }
        void** OriginalOpenSlot() { return &origOpen; }     // handed to the hook engine, which fills it with the trampoline
        bool IsHooked() const { return hooked; }

        /* Test support: forget everything */
        void Reset();

    private:
        StdData();

        struct Candidate
        {
            std::string kind;       // "IDE" / "IPL"
            std::string abs;
            std::string rel;
            std::string baseLower;
            uint32_t    priority;
            uint64_t    modId;
        };

        static int  CbGetBehaviour(modloader_plugin_t*, modloader_file_t*);
        static int  CbInstallFile(modloader_plugin_t*, const modloader_file_t*);
        static int  CbReinstallFile(modloader_plugin_t*, const modloader_file_t*);
        static int  CbUninstallFile(modloader_plugin_t*, const modloader_file_t*);
        static int  CbOnStartup(modloader_plugin_t*);
        static int  CbOnShutdown(modloader_plugin_t*);
        static void CbUpdate(modloader_plugin_t*);

        std::string BuildGtaDat(const char* gameName);

        modloader_plugin_t  plugin;
        Vfs                 vfs;
        std::vector<Candidate> candidates;
        OriginalReader      reader;
        void*               origOpen = nullptr;

        // recursive: the original OS_FileOpen calls itself again (through the hook) when it has to open the OBB
        std::recursive_mutex mtx;
        std::set<std::string> logged;
        std::string         gtaDatPath;         // generated gta.dat (empty until it is built)
        bool                gtaDatBuilt = false;
        bool                hooked = false;
        int                 traceLeft = 0;
        std::set<std::string> traced;
};

/* Is this extension one of the files that std.stream handles (they live inside the IMG archives)? */
bool IsStreamExtension(const std::string& ext);

/* A binary stream IPL starts with "bnry" */
bool IsBinaryIplFile(const std::string& absPath);

} // namespace mla
