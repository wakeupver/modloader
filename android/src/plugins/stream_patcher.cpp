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
    allocatedBuffer = 0;

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

    // The game just loaded this directory into the streaming infos, they must show up in the array we know about. If they do not,
    // something (a limit adjuster, for instance) moved the array and everything we would write would go nowhere.
    size_t visible = 0;
    for(unsigned i = 0; i < gtasa::kNumStreamingInfos; ++i)
        if(api.infos[i].cdSize != 0 && api.infos[i].imgId == (uint8_t)imageIndex) ++visible;
    if(names.size() >= 100 && visible == 0)
    {
        MLA_ERR("std.stream: \"%s\" has %u entries but none of them is in CStreaming::ms_aInfoForModel: the game seems to use another "
                "array (moved by a limit adjuster?). Replacing models inside IMG archives is disabled.", path, (unsigned)names.size());
        enabled.store(false);
        pending.clear();
        return;
    }
    std::vector<PatchTarget> targets = FindPatchTargets(api.infos, gtasa::kNumStreamingInfos, imageIndex, names,
        [this](const std::string& n) { return layout->Find(n) != nullptr; });

    pending.insert(pending.end(), targets.begin(), targets.end());
    MLA_LOG("std.stream: \"%s\" (image %d): %u entries, %u have a replacement", path, imageIndex, (unsigned)names.size(), (unsigned)targets.size());
}

void StreamPatcher::Apply(bool late)
{
    if(pending.empty()) return;
    if(!enabled.load()) { pending.clear(); return; }

    uint32_t maxSectors = 0;
    size_t applied = 0;

    for(auto& t : pending)
    {
        const ImgEntry* e = layout->Find(t.lowerName);
        if(!e || t.infoIndex >= gtasa::kNumStreamingInfos) continue;

        if(late && (int)e->sectors > allocatedBuffer)
        {
            MLA_ERR("std.stream: \"%s\" (%u sectors) is bigger than the streaming buffers of the game (%d sectors), which already exist: skipped",
                    t.lowerName.c_str(), (unsigned)e->sectors, allocatedBuffer);
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
        // CStreaming::Init2 allocates the streaming buffers right after the last directory: this is their size
        allocatedBuffer = api.bufferSize? *api.bufferSize : 0;
    }
    else
    {
        Record(path, imageIndex);
        // An archive whose directory is read after ours (player.img, for instance, is not part of CStreaming::Init2) comes
        // when the streaming buffers are already allocated: what does not fit in them is left alone.
        if(synthLoaded) Apply(true);
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
    allocatedBuffer = api.bufferSize? *api.bufferSize : 0;      // the game is running: the buffers exist
    Apply(true);
}

} // namespace mla
