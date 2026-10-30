/*
 *  Mod Loader - Android port
 *      The streaming side of std.stream, written against a small "game API" so that it can be unit tested with a fake
 *      game (see tests/). What it does, in the order the game does things:
 *
 *        CStreaming::Init2() loads the directory of every IMG archive with CStreaming::LoadCdDirectory(path, index). The game
 *        keeps the FIRST entry it finds for each model/txd/col..., entries of later archives with the same name are
 *        ignored, so an archive cannot override another one. That is why replacing is done by patching the streaming
 *        info after the fact:
 *
 *          1. At the first LoadCdDirectory call the generated archive is registered with AddImageToList in the first free
 *             slot (the loop of the game stops at the first empty slot, so it must be that one). The game then loads its
 *             directory like any other: entries that are new models/textures get registered by the game itself.
 *          2. After each original archive is loaded its directory is read again, and the streaming infos that came from
 *             entries we have a replacement for are remembered (infos hold no names, but they do hold the position of the
 *             entry, which is unique).
 *          3. After the generated archive is loaded those infos are pointed to it (image index, position and size), and
 *             CStreaming::ms_streamingBufferSize is raised if a replacement is bigger than every original entry, because
 *             the streaming buffers are only allocated after the last directory is loaded.
 *
 *      If the streaming system was already initialised when the port got control ("late" mode) the same is done in
 *      one go, but replacements bigger than the (already allocated) streaming buffer are skipped.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include "img.hpp"
#include "../game/gtasa200.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace mla {

class StreamPatcher
{
    public:
        struct Api
        {
            gtasa::StreamingFile*   files = nullptr;        // CStreaming::ms_files   (8 entries)
            gtasa::CStreamingInfo*  infos = nullptr;        // CStreaming::ms_aInfoForModel (26316 entries)
            int*                    bufferSize = nullptr;   // CStreaming::ms_streamingBufferSize (in sectors)

            std::function<int(const char* path, bool notPlayerImg)>                 addImageToList;             // CStreaming::AddImageToList
            std::function<bool(const char* path, std::vector<uint8_t>& directory)>  readDirectory;              // header + directory of an IMG, read the way the game does it
            std::function<void(const char* path, int imageIndex)>                   originalLoadCdDirectory;    // the real CStreaming::LoadCdDirectory
        };

        /* 'layout' must outlive the patcher. 'alias' is the path the game is told to open for the generated archive. */
        void Configure(const Api& api, const ImgLayout* layout, const std::string& alias);

        bool IsEnabled() const { return enabled.load(); }

        /* The hook of CStreaming::LoadCdDirectory(path, imageIndex) */
        void OnLoadCdDirectory(const char* path, int imageIndex);

        /* Is the streaming system already up? (the directories are loaded) */
        bool StreamingAlreadyInitialised() const;

        /* Does everything in one go, for when the streaming system is already up */
        void PatchLate();

        int     ImageIndex()    const { return synthIdx; }
        size_t  PatchedCount()  const { return patchedTotal; }
        size_t  SkippedCount()  const { return skippedTotal; }

    private:
        bool RegisterImage(bool parseDirectory);
        void Record(const char* path, int imageIndex);
        void Apply(bool late);

        Api                         api;
        const ImgLayout*            layout = nullptr;
        std::string                 alias;
        std::atomic<bool>           enabled{false};
        bool                        registered = false;
        bool                        synthLoaded = false;
        int                         synthIdx = -1;
        int                         allocatedBuffer = 0;    // size of the streaming buffers once they exist (the variable of the game keeps growing as more directories are read)
        std::vector<PatchTarget>    pending;
        size_t                      patchedTotal = 0;
        size_t                      skippedTotal = 0;
};

} // namespace mla
