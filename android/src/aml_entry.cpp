/*
 *  Mod Loader - Android port
 *      The AML (Android Mod Loader, https://github.com/RusJJ/AndroidModLoader) plugin that hosts the loader.
 *      This is the only file of the port that talks to AML, everything else only sees core/loader.hpp and game/host.hpp.
 *
 *      Install:  copy libmodloader.so to  /sdcard/Android/data/com.rockstargames.gtasa/mods/   (the AML mods folder)
 *                and put the mods in       /sdcard/Android/data/com.rockstargames.gtasa/files/modloader/<Mod Name>/
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "compat.hpp"

#include <mod/amlmod.h>
#include <mod/logger.h>

#include "core/loader.hpp"
#include "game/gotsym.hpp"
#include "game/host.hpp"
#include "plugins/std_data.hpp"
#include "plugins/std_stream.hpp"
#include "game/gtasa200.hpp"
#include "util/fs.hpp"
#include "util/log.hpp"
#include "util/strutil.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#if !defined(__arm__)
#error "GTA San Andreas for Android 2.00 is a 32 bits ARM game: build this mod for armeabi-v7a"
#endif

MYMOD(com.thelink2012.modloader, Mod Loader, 0.3.11, LINK/2012 and contributors)

static void* g_hGTASA = nullptr;

/* ---- what the plugins need from AML ---- */
static bool HostHook(void* target, void* replacement, void** original)
{
    return aml->Hook(target, replacement, original);
}

static uintptr_t HostSym(const char* name)
{
    return g_hGTASA? aml->GetSym(g_hGTASA, name) : 0;
}

/* The address the code of the game uses for a global variable; libGTASA.so is patched at run time by the Fastman92 loader and some
   of its arrays are not where their exported symbol says */
static mla::gtasa::ImageInfo g_image;
static bool                  g_haveImage = false;

static uintptr_t HostData(const char* name)
{
    uintptr_t exported = HostSym(name);
    if(!g_haveImage) return exported;

    bool relocated = false;
    uintptr_t used = mla::gtasa::ResolveDataSymbol(g_image, name, exported, &relocated);
    if(relocated)
        MLA_LOG("%s is not where it is exported: exported at 0x%08x, the code of the game uses 0x%08x", name, (unsigned)exported, (unsigned)used);
    return used;
}

static void LogSink(int level, const char* line)
{
    if(level == mla::LOG_ERROR)      logger->Error("%s", line);
    else if(level == mla::LOG_DEBUG) { /* only goes to modloader.log */ }
    else                             logger->Info("%s", line);
}

/*
 *  The folder where the game keeps its loose files ("/storage/emulated/0/Android/data/<package>/files").
 *  The game's own idea of it is preferred (StorageRootBuffer is filled by the Java side before the native code runs),
 *  AML's is the fallback.
 */
static std::string FindGameFilesFolder()
{
    if(const char* own = (const char*)HostData("StorageRootBuffer"))
        if(own[0] == '/' && mla::fs::GetStat(own).isDir)
            return own;

    std::string root;
    if(const char* p = aml->GetAndroidDataPath())
        root = p;

    if(root.empty() || !mla::fs::GetStat(root).isDir)
    {
        // Fall back to where Android puts the files folder of a package
        if(const char* pkg = aml->GetCurrentGame())
            root = std::string("/storage/emulated/0/Android/data/") + pkg + "/files";
    }
    return root;
}

/*
 *  What is the library the game is really running? Written to the log so that a build of the game that behaves differently
 *  from the analysed one (see tools/verify_libgtasa.py) can be recognised from modloader.log alone.
 */
static void LogGameLibrary()
{
    uintptr_t base = aml->GetLib("libGTASA.so");
    mla::gtasa::SetLibraryBase(base);
    MLA_LOG("Game: %s, libGTASA.so is loaded at 0x%08x", aml->GetCurrentGame()? aml->GetCurrentGame() : "?", (unsigned)base);

    std::string path;
    if(FILE* maps = std::fopen("/proc/self/maps", "r"))
    {
        char line[768];
        while(std::fgets(line, sizeof(line), maps))
            if(std::strstr(line, "libGTASA.so"))
            {
                if(const char* slash = std::strchr(line, '/')) path = slash;
                break;
            }
        std::fclose(maps);
    }
    while(!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    if(path.empty()) return;

    mla::fs::Stat st = mla::fs::GetStat(path);
    if(st.exists && !st.isDir && st.size < (128u << 20))
    {
        mla::Fnv64 hash;
        if(FILE* f = std::fopen(path.c_str(), "rb"))
        {
            static char buf[1 << 16];
            size_t n;
            while((n = std::fread(buf, 1, sizeof(buf), f)) > 0) hash.Add(buf, n);
            std::fclose(f);
        }
        MLA_LOG("libGTASA.so file: %s (%llu bytes, fnv1a64 %s)", path.c_str(), (unsigned long long)st.size, mla::Hex64(hash.h).c_str());
    }
    else
        MLA_LOG("libGTASA.so file: %s", path.c_str());
}

ON_MOD_LOAD()
{
    logger->SetTag("Mod Loader");

    g_hGTASA = aml->GetLibHandle("libGTASA.so");
    if(!g_hGTASA)
    {
        logger->Error("libGTASA.so is not loaded, Mod Loader only works on GTA San Andreas");
        return;
    }

    mla::gtasa::SetLibraryBase(aml->GetLib("libGTASA.so"));
    g_haveImage = mla::gtasa::FindLoadedImage("libGTASA.so", &g_image);
    static const mla::Host host = { &HostHook, &HostSym, &HostData };
    mla::SetHost(&host);
    mla::Log::Get().SetSink(&LogSink);

    std::string root = FindGameFilesFolder();
    if(root.empty() || !mla::Loader::Get().Init(root))
    {
        logger->Error("Could not use \"%s\" as the Mod Loader folder", root.c_str());
        return;
    }

    LogGameLibrary();

    mla::Loader& loader = mla::Loader::Get();
    loader.RegisterPlugin(mla::StdData::Get().Plugin());
    loader.RegisterPlugin(mla::StdStream::Get().Plugin());
    loader.Start();
}

ON_MOD_UNLOAD()
{
    mla::Loader::Get().Shutdown();
    mla::Log::Get().Close();
}

/* If the game goes down, leave something useful in modloader.log (the address is relative to the library) */
ON_GAME_CRASH()
{
    // Runs inside a signal handler: never wait for a lock that the crashed thread may be holding
    mla::Log::Get().TryWrite(mla::LOG_ERROR, "The game crashed: lib=%s signal=%d code=%d pc=%p (offset inside the library: 0x%x)",
                             library? library : "?", sig, code, mcontext? (void*)mcontext->arm_pc : nullptr, (unsigned)libaddr);
}
