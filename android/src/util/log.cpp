/*
 *  Mod Loader - Android port
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#include "log.hpp"
#include "fs.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace mla {

struct Log::Impl
{
    std::mutex mtx;
    FILE*      file = nullptr;
    bool       flushEach = true;
    uint64_t   maxSize = 0;
    uint64_t   written = 0;
    bool       truncatedNoted = false;
    Sink       sink = nullptr;
};

Log::Log() : impl(new Impl) {}
Log::~Log() { Close(); delete impl; }

Log& Log::Get()
{
    static Log instance;
    return instance;
}

void Log::SetSink(Sink sink)
{
    std::lock_guard<std::mutex> lock(impl->mtx);
    impl->sink = sink;
}

void Log::Open(const std::string& path, bool fileEnabled, uint64_t maxSize, bool flushEachLine)
{
    std::lock_guard<std::mutex> lock(impl->mtx);

    if(impl->file) { std::fclose(impl->file); impl->file = nullptr; }
    impl->flushEach = flushEachLine;
    impl->maxSize   = maxSize;
    impl->written   = 0;
    impl->truncatedNoted = false;

    if(!fileEnabled) return;

    // Keep the log of the previous run around, it is very useful after a crash
    if(fs::Exists(path))
    {
        std::string old = path + ".old";
        std::remove(old.c_str());
        fs::RenameFile(path, old);
    }

    impl->file = std::fopen(path.c_str(), "wb");
}

void Log::Close()
{
    std::lock_guard<std::mutex> lock(impl->mtx);
    if(impl->file) { std::fclose(impl->file); impl->file = nullptr; }
}

void Log::Flush()
{
    std::lock_guard<std::mutex> lock(impl->mtx);
    if(impl->file) std::fflush(impl->file);
}

void Log::Write(int level, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    WriteV(level, fmt, va);
    va_end(va);
}

bool Log::TryWrite(int level, const char* fmt, ...)
{
    char line[1536];
    va_list va;
    va_start(va, fmt);
    std::vsnprintf(line, sizeof(line), fmt, va);
    va_end(va);

    std::unique_lock<std::mutex> lock(impl->mtx, std::try_to_lock);
    if(!lock.owns_lock()) return false;
    EmitLocked(level, line);
    return true;
}

void Log::WriteV(int level, const char* fmt, va_list va)
{
    char line[1536];
    std::vsnprintf(line, sizeof(line), fmt, va);

    std::lock_guard<std::mutex> lock(impl->mtx);
    EmitLocked(level, line);
}

void Log::EmitLocked(int level, const char* line)
{
    if(impl->sink)
        impl->sink(level, line);

    if(impl->file)
    {
        if(impl->maxSize && impl->written >= impl->maxSize)
        {
            if(!impl->truncatedNoted)
            {
                impl->truncatedNoted = true;
                std::fputs("\n[log size limit reached, further messages go to logcat only]\n", impl->file);
                std::fflush(impl->file);
            }
            return;
        }

        std::time_t t = std::time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tmv);

        const char* tag = (level == LOG_ERROR)? "ERROR" : (level == LOG_DEBUG)? "debug" : "info ";
        int n = std::fprintf(impl->file, "[%s][%s] %s\n", stamp, tag, line);
        if(n > 0) impl->written += (uint64_t)n;
        if(impl->flushEach) std::fflush(impl->file);
    }
}

} // namespace mla
