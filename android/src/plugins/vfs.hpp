/*
 *  Mod Loader - Android port
 *      The virtual file system of std.data: maps "a file the game asks for" to "a file inside a mod".
 *
 *      A mod file is reachable by:
 *        - its exact path inside the mod folder  ("data/handling.cfg"  -> game file "DATA\\HANDLING.CFG")
 *        - or, for data files, just by its name   ("Cars/handling.cfg" -> game file "DATA\\HANDLING.CFG")
 *          (like the original, the mod folder can be organised any way the mod author wants)
 *        - or through an alias, a made up path that the port itself puts in places the game reads (IDE/IPL lines
 *          injected into gta.dat, the generated IMG archive...)
 *
 *      When two mods provide the same file the one with the higher priority wins; between mods with the same priority
 *      the one that comes last in alphabetical order wins.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace mla {

struct VfsFile
{
    std::string abs;            // absolute path on disk
    std::string rel;            // path inside the mod folder (original case)
    std::string modName;
    uint32_t    priority = 0;   // priority of the mod
    uint64_t    modId    = 0;   // position of the mod in the sorted mod list (higher wins ties)
    uint64_t    size     = 0;
};

class Vfs
{
    public:
        /* 'allowName' makes the file reachable by its file name alone, besides its exact path. */
        void Add(const VfsFile& file, bool allowName);

        /* Aliases are fake game paths that resolve to a real file. */
        void AddAlias(const std::string& aliasPath, const std::string& abs);

        /* 'key' is NormalizeKey(path the game asked for), returns null when no mod provides it */
        const VfsFile* Find(const std::string& key) const;

        size_t NumExact() const { return exact.size(); }
        size_t NumByName() const { return byName.size(); }
        size_t NumAliases() const { return alias.size(); }

        /* True if 'a' wins over 'b' */
        static bool Wins(const VfsFile& a, const VfsFile& b);

    private:
        static void Put(std::unordered_map<std::string, VfsFile>& map, const std::string& key, const VfsFile& file);

        std::unordered_map<std::string, VfsFile> exact;
        std::unordered_map<std::string, VfsFile> byName;
        std::unordered_map<std::string, VfsFile> alias;
};

} // namespace mla
