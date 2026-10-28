/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "stream_patcher.hpp"
#include "../util/log.hpp"

#include <algorithm>
#include <cstring>

namespace mla {

void StreamPatcher::Configure(const Api& a, const ImgLayout* l, const std::string& imageAlias)
{
    api = a;
    layout = l;
    alias = imageAlias;
    registered = false;
    synthLoaded = false;
    synthIdx = -1;
    pending.clear();
    patchedTotal = skippedTotal = 0;

    bool ok = api.files && api.infos && api.addImageToList && api.readDirectory && api.originalLoadCdDirectory && layout && !layout->entries.empty();
    enabled.store(ok);
}

bool StreamPatcher::StreamingAlreadyInitialised() const
{
    if(!api.files || !api.infos || api.files[0].name[0] == '\0')
        return false;

    unsigned used = 0;
    for(unsigned i = 0; i < gtasa::kNumStreamingInfos && used < 100; ++i)
        if(api.infos[i].cdSize != 0) ++used;
    return used >= 100;
}

bool StreamPatcher::RegisterImage(bool parseDirectory)
{
    // The game walks ms_files from the beginning up to the first empty slot, so the archive has to go in the first free one
    int slot = -1;
    for(int i = 0; i < (int)gtasa::kMaxStreamingFiles; ++i)
        if(api.files[i].name[0] == '\0') { slot = i; break; }

    if(slot < 0)
    {
        MLA_ERR("std.stream: all %u image slots of the game are in use, cannot add the generated archive", (unsigned)gtasa::kMaxStreamingFiles);
        return false;
    }
    if(slot >= (int)gtasa::kMaxImageSlotForOffsets)
    {
        MLA_ERR("std.stream: the first free image slot is %d but the game only keeps offsets for %u images", slot, (unsigned)gtasa::kMaxImageSlotForOffsets);
        return false;
    }

    int got = api.addImageToList(alias.c_str(), parseDirectory);

    // CdStreamOpen returns 0 when the file could not be opened, and "0" is the handle of gta3.img: never leave that behind
    bool good = (got == slot) && api.files[slot].name[0] != '\0' && api.files[slot].streamHandle != 0;
    if(!good)
    {
        MLA_ERR("std.stream: the game could not open the generated archive (AddImageToList returned %d for slot %d, handle %d)",
                got, slot, (int)api.files[slot].streamHandle);
        api.files[slot].name[0]      = '\0';
        api.files[slot].streamHandle = 0;
        api.files[slot].notPlayerImg = 0;
        return false;
    }

    synthIdx = slot;
    MLA_LOG("std.stream: generated archive registered as image %d (%u entries)", slot, (unsigned)layout->entries.size());
    return true;
}

void StreamPatcher::Record(const char* path, int imageIndex)
{
    std::vector<uint8_t> dir;
    if(!api.readDirectory(path, dir))
    {
        MLA_ERR("std.stream: could not read the directory of \"%s\", its models will not be replaced", path);
        return;
    }

    std::vector<ImgDirName> names = ParseImgDirectory(dir.data(), dir.size());
    std::vector<PatchTarget> targets = FindPatchTargets(api.infos, gtasa::kNumStreamingInfos, imageIndex, names,
        [this](const std::string& n) { return layout->Find(n) != nullptr; });

    pending.insert(pending.end(), targets.begin(), targets.end());
    MLA_LOG("std.stream: \"%s\" (image %d): %u entries, %u have a replacement", path, imageIndex, (unsigned)names.size(), (unsigned)targets.size());
}

void StreamPatcher::Apply(bool late)
{
    if(pending.empty()) return;

    uint32_t maxSectors = 0;
    size_t applied = 0;

    for(auto& t : pending)
    {
        const ImgEntry* e = layout->Find(t.lowerName);
        if(!e || t.infoIndex >= gtasa::kNumStreamingInfos) continue;

        if(late && api.bufferSize && (int)e->sectors > *api.bufferSize)
        {
            MLA_ERR("std.stream: \"%s\" (%u sectors) is bigger than the streaming buffer of the game (%d sectors) and "
                    "the game was already running when the loader started, skipped", t.lowerName.c_str(), (unsigned)e->sectors, *api.bufferSize);
            ++skippedTotal;
            continue;
        }

        ApplyPatch(api.infos[t.infoIndex], synthIdx, *e);
        maxSectors = std::max(maxSectors, e->sectors);
        ++applied;
        MLA_DBG("std.stream: replaced %s (streaming info %u)", t.lowerName.c_str(), (unsigned)t.infoIndex);
    }
    pending.clear();
    patchedTotal += applied;

    if(!late && api.bufferSize && *api.bufferSize < (int)maxSectors)
    {
        MLA_LOG("std.stream: streaming buffer raised from %d to %u sectors", *api.bufferSize, (unsigned)maxSectors);
        *api.bufferSize = (int)maxSectors;
    }

    MLA_LOG("std.stream: %u entries now stream from the generated archive", (unsigned)applied);
}

void StreamPatcher::OnLoadCdDirectory(const char* path, int imageIndex)
{
    if(!enabled.load())
    {
        api.originalLoadCdDirectory(path, imageIndex);
        return;
    }

    if(!registered)
    {
        registered = true;
        if(!RegisterImage(/*parseDirectory=*/true))
            enabled.store(false);
    }

    api.originalLoadCdDirectory(path, imageIndex);
    if(!enabled.load()) return;

    if(imageIndex == synthIdx)
    {
        synthLoaded = true;
        Apply(false);
    }
    else
    {
        Record(path, imageIndex);
        if(synthLoaded) Apply(false);       // an archive that comes after ours (not expected, but cheap to support)
    }
}

void StreamPatcher::PatchLate()
{
    if(!enabled.load() || registered) return;
    registered = true;

    if(!RegisterImage(/*parseDirectory=*/false))
    {
        enabled.store(false);
        return;
    }

    for(int i = 0; i < (int)gtasa::kMaxStreamingFiles; ++i)
    {
        if(i == synthIdx || api.files[i].name[0] == '\0' || !api.files[i].notPlayerImg)
            continue;
        Record(api.files[i].name, i);
    }

    synthLoaded = true;
    Apply(true);
}

} // namespace mla
