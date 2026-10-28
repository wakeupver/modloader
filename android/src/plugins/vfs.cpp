/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "vfs.hpp"
#include "../util/strutil.hpp"

namespace mla {

bool Vfs::Wins(const VfsFile& a, const VfsFile& b)
{
    if(a.priority != b.priority) return a.priority > b.priority;
    if(a.modId    != b.modId)    return a.modId    > b.modId;
    return ToLower(a.rel) > ToLower(b.rel);        // same mod, two files with the same key: be deterministic
}

void Vfs::Put(std::unordered_map<std::string, VfsFile>& map, const std::string& key, const VfsFile& file)
{
    auto it = map.find(key);
    if(it == map.end())            map.emplace(key, file);
    else if(Wins(file, it->second)) it->second = file;
}

void Vfs::Add(const VfsFile& file, bool allowName)
{
    std::string rel = NormalizeKey(file.rel);
    if(rel.empty()) return;

    Put(exact, rel, file);
    if(allowName)
        Put(byName, BaseName(rel), file);
}

void Vfs::AddAlias(const std::string& aliasPath, const std::string& abs)
{
    VfsFile f;
    f.abs = abs;
    f.rel = aliasPath;
    f.modName = "(modloader)";
    f.priority = 0xFFFFFFFFu;
    alias[NormalizeKey(aliasPath)] = f;
}

const VfsFile* Vfs::Find(const std::string& key) const
{
    auto a = alias.find(key);
    if(a != alias.end()) return &a->second;

    auto e = exact.find(key);
    if(e != exact.end()) return &e->second;

    auto n = byName.find(BaseName(key));
    if(n != byName.end()) return &n->second;

    return nullptr;
}

} // namespace mla
