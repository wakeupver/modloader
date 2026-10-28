/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "img.hpp"
#include "../util/strutil.hpp"
#include "../util/fs.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

namespace mla {

using gtasa::kSectorSize;

static const char* const kImgSigTag = "mla-img-1";

const ImgEntry* ImgLayout::Find(const std::string& lowerName) const
{
    auto it = std::lower_bound(entries.begin(), entries.end(), lowerName,
                               [](const ImgEntry& e, const std::string& n) { return e.name < n; });
    return (it != entries.end() && it->name == lowerName)? &*it : nullptr;
}

static uint32_t SectorsFor(uint64_t bytes)
{
    return (uint32_t)((bytes + kSectorSize - 1) / kSectorSize);
}

ImgLayout LayoutImg(const std::vector<ImgSource>& sources, std::vector<std::string>* skipped)
{
    ImgLayout layout;

    std::map<std::string, const ImgSource*> byName;     // sorted + deduplicated (last one wins)
    for(auto& s : sources)
    {
        std::string lname = ToLower(s.name);
        if(lname.empty() || lname.size() > 23)
        {
            if(skipped) skipped->push_back(s.name + " (the IMG format only allows names up to 23 characters)");
            continue;
        }
        if(s.size == 0)
        {
            if(skipped) skipped->push_back(s.name + " (empty file)");
            continue;
        }
        if(SectorsFor(s.size) > 0xFFFF)
        {
            if(skipped) skipped->push_back(s.name + " (bigger than 128 MB, too big for an IMG entry)");
            continue;
        }
        byName[lname] = &s;
    }

    // header + directory, padded to a whole sector
    uint64_t dirBytes = 8 + 32ull * byName.size();
    uint32_t cursor = SectorsFor(dirBytes);
    if(byName.empty()) cursor = 1;

    Fnv64 sig;
    sig.Add(std::string(kImgSigTag));

    for(auto& kv : byName)
    {
        const ImgSource& s = *kv.second;

        ImgEntry e;
        e.name    = kv.first;
        e.abs     = s.abs;
        e.size    = s.size;
        e.offset  = cursor;
        e.sectors = SectorsFor(s.size);
        cursor   += e.sectors;
        layout.maxSectors = std::max(layout.maxSectors, e.sectors);

        sig.Add(e.name);
        sig.Add(e.abs);
        sig.Add(e.size);
        sig.Add((uint64_t)s.mtime);

        layout.entries.push_back(e);
    }

    layout.totalSize = (uint64_t)cursor * kSectorSize;
    layout.signature = Hex64(sig.h);
    return layout;
}

static void Put16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void Put32(uint8_t* p, uint32_t v) { Put16(p, v & 0xFFFF); Put16(p + 2, v >> 16); }

bool WriteImg(const std::string& path, const ImgLayout& layout)
{
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if(!f) return false;

    bool ok = true;

    // header + directory
    uint64_t dirBytes = 8 + 32ull * layout.entries.size();
    uint64_t firstData = layout.entries.empty()? (uint64_t)kSectorSize : (uint64_t)layout.entries.front().offset * kSectorSize;
    std::vector<uint8_t> head((size_t)std::max<uint64_t>(firstData, SectorsFor(dirBytes) * (uint64_t)kSectorSize), 0);

    std::memcpy(head.data(), "VER2", 4);
    Put32(head.data() + 4, (uint32_t)layout.entries.size());

    size_t pos = 8;
    for(auto& e : layout.entries)
    {
        Put32(&head[pos], e.offset);
        Put16(&head[pos + 4], e.sectors);              // streaming size
        Put16(&head[pos + 6], 0);                      // size in archive, the game uses the streaming size when this is 0
        std::memcpy(&head[pos + 8], e.name.c_str(), e.name.size());       // zero padded (the vector is zero filled), name <= 23 chars
        pos += 32;
    }
    ok = std::fwrite(head.data(), 1, head.size(), f) == head.size();

    // data
    std::vector<char> buf(256 * 1024);
    static const char zeros[kSectorSize] = {0};

    for(size_t i = 0; ok && i < layout.entries.size(); ++i)
    {
        const ImgEntry& e = layout.entries[i];
        FILE* in = std::fopen(e.abs.c_str(), "rb");
        if(!in) { ok = false; break; }

        uint64_t copied = 0;
        size_t n;
        while(ok && (n = std::fread(buf.data(), 1, buf.size(), in)) > 0)
        {
            ok = std::fwrite(buf.data(), 1, n, f) == n;
            copied += n;
        }
        std::fclose(in);

        if(copied != e.size) ok = false;        // the file changed while we were reading it

        uint64_t padding = (uint64_t)e.sectors * kSectorSize - copied;
        if(ok && copied <= (uint64_t)e.sectors * kSectorSize && padding > 0)
            ok = std::fwrite(zeros, 1, (size_t)padding, f) == (size_t)padding;
    }

    ok = (std::fflush(f) == 0) && ok;
    ok = (std::fclose(f) == 0) && ok;

    if(ok) ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    if(!ok) std::remove(tmp.c_str());
    return ok;
}

bool WriteImgSignature(const std::string& sigPath, const ImgLayout& layout)
{
    return fs::WriteFileAtomic(sigPath, layout.signature);
}

bool IsImgUpToDate(const std::string& imgPath, const std::string& sigPath, const ImgLayout& layout)
{
    fs::Stat st = fs::GetStat(imgPath);
    if(!st.exists || st.size != layout.totalSize) return false;

    std::string sig;
    if(!fs::ReadFile(sigPath, sig, 256)) return false;
    return Trim(sig) == layout.signature;
}

std::vector<ImgDirName> ParseImgDirectory(const uint8_t* data, size_t size)
{
    std::vector<ImgDirName> out;
    if(!data || size < 8) return out;

    uint32_t count = (uint32_t)data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    if(count > (size - 8) / 32) count = (uint32_t)((size - 8) / 32);

    out.reserve(count);
    for(uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* e = data + 8 + (size_t)i * 32;
        ImgDirName n;
        n.offset = (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);

        char name[24];
        std::memcpy(name, e + 8, 23);
        name[23] = '\0';                                    // the game does the same
        n.lowerName = ToLower(name);
        out.push_back(n);
    }
    return out;
}

std::vector<PatchTarget> FindPatchTargets(const gtasa::CStreamingInfo* infos, size_t numInfos, int imgIndex,
                                          const std::vector<ImgDirName>& directory,
                                          const std::function<bool(const std::string&)>& wanted)
{
    std::vector<PatchTarget> out;

    // position -> wanted names
    std::unordered_multimap<uint32_t, const std::string*> byPosn;
    for(auto& d : directory)
        if(wanted(d.lowerName))
            byPosn.emplace(d.offset, &d.lowerName);

    if(byPosn.empty())
        return out;

    for(size_t i = 0; i < numInfos; ++i)
    {
        const gtasa::CStreamingInfo& info = infos[i];
        if(info.cdSize == 0 || info.imgId != (uint8_t)imgIndex) continue;

        auto range = byPosn.equal_range(info.cdPosn);
        for(auto it = range.first; it != range.second; ++it)
        {
            PatchTarget t;
            t.infoIndex = (uint32_t)i;
            t.lowerName = *it->second;
            out.push_back(t);
        }
    }
    return out;
}

void ApplyPatch(gtasa::CStreamingInfo& info, int imgIndex, const ImgEntry& entry)
{
    info.imgId  = (uint8_t)imgIndex;
    info.cdPosn = entry.offset;
    info.cdSize = entry.sectors;
}

} // namespace mla
