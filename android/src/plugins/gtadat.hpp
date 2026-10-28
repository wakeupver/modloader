/*
 *  Mod Loader - Android port
 *      gta.dat handling for std.data.
 *
 *      Mods can ship .ide and .ipl files that are not replacements of anything in the game. The game only loads what
 *      gta.dat lists, so when the game opens "data/gta.dat" std.data serves a copy that has one extra line for every
 *      one of those files. The new lines point to alias paths ("modloader/.vfs/...", see Vfs::AddAlias), so mod
 *      folders with spaces in their names are not a problem for the game's line parser.
 *
 *      The IDE lines go right before the first IPL line (models have to exist when the maps are loaded) and the IPL
 *      lines go to the end of the file.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include <string>
#include <vector>
#include <set>

namespace mla {

struct GtaDatItem
{
    std::string kind;       // "IDE" or "IPL"
    std::string path;       // what goes after the keyword
};

/* Lower case file names of everything listed by IDE/IPL lines of a gta.dat/default.dat text */
std::set<std::string> GtaDatReferencedNames(const std::string& dat);

/* Returns 'original' with the extra lines inserted */
std::string MergeGtaDat(const std::string& original, const std::vector<GtaDatItem>& items);

} // namespace mla
