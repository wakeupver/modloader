/*
 *  Mod Loader - Android port
 *      String / path helpers.
 *
 *  Paths inside the loader are always kept with '/' as separator. The game itself is a bit messy about it
 *  (it asks for "MODELS\\GTA3.IMG", "MPACK//MPACK1//TEXT.GXT" and so on), so everything that comes from the
 *  game goes through NormalizePath/NormalizeKey before being compared with anything.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include <string>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <cstdio>

namespace mla {

inline std::string ToLower(std::string s)
{
    for(auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

/*
 *  Makes a path look like "dir/sub/file.ext":
 *      '\' -> '/', repeated separators collapsed, leading "./" removed, trailing spaces trimmed.
 *  The case is preserved.
 */
inline std::string NormalizePath(const char* path)
{
    std::string out;
    if(!path) return out;
    out.reserve(std::strlen(path));

    bool lastWasSlash = false;
    for(const char* p = path; *p; ++p)
    {
        char c = (*p == '\\')? '/' : *p;
        if(c == '/')
        {
            if(lastWasSlash) continue;
            lastWasSlash = true;
        }
        else lastWasSlash = false;
        out.push_back(c);
    }

    while(out.size() >= 2 && out[0] == '.' && out[1] == '/') out.erase(0, 2);
    while(!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

inline std::string NormalizePath(const std::string& path) { return NormalizePath(path.c_str()); }

/* Normalized + lower case, this is the key used on every lookup table */
inline std::string NormalizeKey(const char* path)         { return ToLower(NormalizePath(path)); }
inline std::string NormalizeKey(const std::string& path)  { return ToLower(NormalizePath(path.c_str())); }

inline std::string BaseName(const std::string& p)
{
    size_t i = p.find_last_of('/');
    return (i == std::string::npos)? p : p.substr(i + 1);
}

inline std::string DirName(const std::string& p)
{
    size_t i = p.find_last_of('/');
    return (i == std::string::npos)? std::string() : p.substr(0, i);
}

/* Lower case extension without the dot ("" if none) */
inline std::string Extension(const std::string& p)
{
    std::string name = BaseName(p);
    size_t i = name.find_last_of('.');
    return (i == std::string::npos)? std::string() : ToLower(name.substr(i + 1));
}

inline bool StartsWith(const std::string& s, const char* prefix)
{
    size_t n = std::strlen(prefix);
    return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

inline bool EndsWith(const std::string& s, const char* suffix)
{
    size_t n = std::strlen(suffix);
    return s.size() >= n && std::memcmp(s.data() + s.size() - n, suffix, n) == 0;
}

inline std::string Trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while(b < e && std::isspace((unsigned char)s[b])) ++b;
    while(e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

/*
 *  Case insensitive wildcard match ('*' = any sequence, '?' = any single character).
 *  Used for the IgnoreFiles / IgnoreMods / IncludeMods... lists of modloader.ini.
 */
inline bool WildcardMatch(const char* pat, const char* str)
{
    const char *star = nullptr, *resume = nullptr;

    while(*str)
    {
        if(*pat == '*')
        {
            star = pat++;
            resume = str;
        }
        else if(*pat == '?' || std::tolower((unsigned char)*pat) == std::tolower((unsigned char)*str))
        {
            ++pat; ++str;
        }
        else if(star)
        {
            pat = star + 1;
            str = ++resume;
        }
        else return false;
    }

    while(*pat == '*') ++pat;
    return *pat == '\0';
}

inline bool WildcardMatch(const std::string& pat, const std::string& str)
{
    return WildcardMatch(pat.c_str(), str.c_str());
}

/* Hex dump of a 64 bit value, used for cache signatures */
inline std::string Hex64(uint64_t v)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return buf;
}

/* FNV-1a 64 bits, incremental */
struct Fnv64
{
    uint64_t h = 14695981039346656037ULL;      // standard FNV-1a 64 offset basis
    void Add(const void* data, size_t len)
    {
        const unsigned char* p = (const unsigned char*)data;
        for(size_t i = 0; i < len; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    }
    void Add(const std::string& s) { Add(s.data(), s.size()); Add("\0", 1); }
    void Add(uint64_t v)           { Add(&v, sizeof(v)); }
};

} // namespace mla
