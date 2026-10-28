/*
 *  Mod Loader - Android port
 *      IMG helpers for std.stream. Everything here is plain C++ (no game involved) so it can be unit tested.
 *
 *      std.stream gathers the models/textures/collisions/animations that mods provide and packs them in one
 *      IMG v2 archive ("modloader/.data/cache/stream.img"). The game is then told to use that archive for those
 *      entries (see std_stream.cpp). The archive is only rewritten when the set of files changes.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include "../game/gtasa200.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mla {

struct ImgSource
{
    std::string name;       // entry name, e.g. "infernus.dff" (any case)
    std::string abs;        // file on disk
    uint64_t    size  = 0;
    int64_t     mtime = 0;
};

struct ImgEntry
{
    std::string name;       // lower case
    std::string abs;
    uint64_t    size    = 0;
    uint32_t    offset  = 0;    // in sectors, from the beginning of the archive
    uint32_t    sectors = 0;
};

struct ImgLayout
{
    std::vector<ImgEntry>   entries;        // sorted by name
    uint64_t                totalSize = 0;  // size of the archive in bytes
    uint32_t                maxSectors = 0; // biggest entry
    std::string             signature;      // changes whenever the archive content would change

    const ImgEntry* Find(const std::string& lowerName) const;
};

/* Decides where every file goes inside the archive. Files that cannot be stored are left out and reported. */
ImgLayout LayoutImg(const std::vector<ImgSource>& sources, std::vector<std::string>* skipped);

/* Writes the archive (IMG version 2). */
bool WriteImg(const std::string& path, const ImgLayout& layout);

/* True when 'imgPath' was already written for this very layout ('sigPath' holds the signature). */
bool IsImgUpToDate(const std::string& imgPath, const std::string& sigPath, const ImgLayout& layout);
bool WriteImgSignature(const std::string& sigPath, const ImgLayout& layout);

/* ---- what the hooks do inside the game, as pure functions ---- */

struct ImgDirName
{
    std::string lowerName;
    uint32_t    offset;     // sectors
};

/* 'data' is the beginning of an IMG v2 file: "VER2", the entry count and the entries. */
std::vector<ImgDirName> ParseImgDirectory(const uint8_t* data, size_t size);

struct PatchTarget
{
    uint32_t    infoIndex;
    std::string lowerName;
};

/*
 *  Which CStreamingInfo entries came from the image 'imgIndex' and carry one of the wanted names.
 *  (the game stores no names, but it does store the position of each entry of the directory, and that is unique)
 */
std::vector<PatchTarget> FindPatchTargets(const gtasa::CStreamingInfo* infos, size_t numInfos, int imgIndex,
                                          const std::vector<ImgDirName>& directory,
                                          const std::function<bool(const std::string&)>& wanted);

/* Points one streaming info to an entry of the generated archive (which is the image 'imgIndex' of the game) */
void ApplyPatch(gtasa::CStreamingInfo& info, int imgIndex, const ImgEntry& entry);

} // namespace mla
