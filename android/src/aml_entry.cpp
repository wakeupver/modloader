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
#include "game/host.hpp"
#include "plugins/std_data.hpp"
#include "plugins/std_stream.hpp"
#include "util/fs.hpp"
#include "util/log.hpp"

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
    if(const char* own = (const char*)HostSym("StorageRootBuffer"))
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

ON_MOD_LOAD()
{
    logger->SetTag("Mod Loader");

    g_hGTASA = aml->GetLibHandle("libGTASA.so");
    if(!g_hGTASA)
    {
        logger->Error("libGTASA.so is not loaded, Mod Loader only works on GTA San Andreas");
        return;
    }

    static const mla::Host host = { &HostHook, &HostSym };
    mla::SetHost(&host);
    mla::Log::Get().SetSink(&LogSink);

    std::string root = FindGameFilesFolder();
    if(root.empty() || !mla::Loader::Get().Init(root))
    {
        logger->Error("Could not use \"%s\" as the Mod Loader folder", root.c_str());
        return;
    }

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
