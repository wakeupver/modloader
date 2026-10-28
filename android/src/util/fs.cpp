/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "fs.hpp"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace mla { namespace fs {

Stat GetStat(const std::string& path)
{
    Stat s;
    struct stat st;
    if(::stat(path.c_str(), &st) == 0)
    {
        s.exists = true;
        s.isDir  = S_ISDIR(st.st_mode);
        s.size   = (uint64_t)st.st_size;
        s.mtime  = (int64_t)st.st_mtime;
    }
    return s;
}

bool ListDir(const std::string& dir, std::vector<std::pair<std::string, bool>>& out)
{
    DIR* d = ::opendir(dir.c_str());
    if(!d) return false;

    while(struct dirent* e = ::readdir(d))
    {
        const char* name = e->d_name;
        if(name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        bool isDir = false;
        if(e->d_type == DT_DIR) isDir = true;
        else if(e->d_type == DT_UNKNOWN || e->d_type == DT_LNK)   // some file systems (FUSE/sdcardfs) don't fill d_type
            isDir = GetStat(Join(dir, name)).isDir;

        out.emplace_back(name, isDir);
    }

    ::closedir(d);
    return true;
}

bool MakeDirs(const std::string& dir)
{
    if(dir.empty()) return false;

    std::string cur;
    cur.reserve(dir.size());
    for(size_t i = 0; i < dir.size(); ++i)
    {
        cur.push_back(dir[i]);
        if(dir[i] == '/' || i + 1 == dir.size())
        {
            if(cur == "/" ) continue;
            if(::mkdir(cur.c_str(), 0777) != 0 && errno != EEXIST)
                return false;
        }
    }
    return GetStat(dir).isDir;
}

bool ReadFile(const std::string& path, std::string& out, size_t maxBytes)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if(!f) return false;

    out.clear();
    char buf[16384];
    size_t n;
    bool ok = true;
    while((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
    {
        if(out.size() + n > maxBytes) { ok = false; break; }
        out.append(buf, n);
    }
    if(std::ferror(f)) ok = false;
    std::fclose(f);
    return ok;
}

size_t ReadPrefix(const std::string& path, void* buffer, size_t count)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if(!f) return 0;
    size_t n = std::fread(buffer, 1, count, f);
    std::fclose(f);
    return n;
}

bool WriteFileAtomic(const std::string& path, const void* data, size_t size)
{
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if(!f) return false;

    bool ok = (size == 0) || (std::fwrite(data, 1, size, f) == size);
    ok = (std::fflush(f) == 0) && ok;
    ok = (std::fclose(f) == 0) && ok;

    if(ok) ok = (std::rename(tmp.c_str(), path.c_str()) == 0);
    if(!ok) std::remove(tmp.c_str());
    return ok;
}

bool RemoveFile(const std::string& path)
{
    return std::remove(path.c_str()) == 0;
}

bool RenameFile(const std::string& from, const std::string& to)
{
    return std::rename(from.c_str(), to.c_str()) == 0;
}

std::string Join(const std::string& a, const std::string& b)
{
    if(a.empty()) return b;
    if(b.empty()) return a;
    if(a.back() == '/') return a + b;
    return a + "/" + b;
}

}} // namespace mla::fs
