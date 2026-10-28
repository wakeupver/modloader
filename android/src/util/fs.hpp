/*
 *  Mod Loader - Android port
 *      Small POSIX filesystem layer (upstream's version of this lives in modloader/util/path.hpp and is
 *      built on top of the Win32 API).
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>

namespace mla { namespace fs {

struct Stat
{
    bool     exists = false;
    bool     isDir  = false;
    uint64_t size   = 0;
    int64_t  mtime  = 0;      // seconds since epoch
};

Stat  GetStat(const std::string& path);
inline bool Exists(const std::string& path) { return GetStat(path).exists; }

/* Lists the entries of a directory (without "." and ".."), returns pairs of (name, is_directory) */
bool  ListDir(const std::string& dir, std::vector<std::pair<std::string, bool>>& out);

/* mkdir -p */
bool  MakeDirs(const std::string& dir);

/* Reads up to maxBytes (fails if the file is bigger than that) */
bool  ReadFile(const std::string& path, std::string& out, size_t maxBytes = 64u * 1024 * 1024);

/* Reads at most 'count' bytes from the beginning of a file, returns the number of bytes read */
size_t ReadPrefix(const std::string& path, void* buffer, size_t count);

/* Writes to "path.tmp" then renames, so a crash never leaves a half written file behind */
bool  WriteFileAtomic(const std::string& path, const void* data, size_t size);
inline bool WriteFileAtomic(const std::string& path, const std::string& data)
{ return WriteFileAtomic(path, data.data(), data.size()); }

bool  RemoveFile(const std::string& path);
bool  RenameFile(const std::string& from, const std::string& to);

std::string Join(const std::string& a, const std::string& b);

}} // namespace mla::fs
