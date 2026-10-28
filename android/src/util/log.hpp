/*
 *  Mod Loader - Android port
 *      Logging: writes to modloader/modloader.log and, through an optional sink, to logcat.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include "../compat.hpp"
#include <string>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace mla {

enum LogLevel { LOG_INFO = 0, LOG_ERROR = 1, LOG_DEBUG = 2 };

class Log
{
    public:
        typedef void (*Sink)(int level, const char* line);

        static Log& Get();

        /* Opens (and rotates the previous) log file. Safe to call more than once. */
        void Open(const std::string& path, bool fileEnabled, uint64_t maxSize, bool flushEachLine);
        void SetSink(Sink sink);
        void Close();
        void Flush();

        void Write(int level, const char* fmt, ...) MLA_PRINTF_FMT(3, 4);
        void WriteV(int level, const char* fmt, va_list va);

        /* Like Write but gives up instead of waiting when another thread holds the log (for signal/crash handlers) */
        bool TryWrite(int level, const char* fmt, ...) MLA_PRINTF_FMT(3, 4);

    private:
        Log();
        ~Log();
        void EmitLocked(int level, const char* line);
        struct Impl;
        Impl* impl;
};

} // namespace mla

#define MLA_LOG(...)    ::mla::Log::Get().Write(::mla::LOG_INFO,  __VA_ARGS__)
#define MLA_ERR(...)    ::mla::Log::Get().Write(::mla::LOG_ERROR, __VA_ARGS__)
#define MLA_DBG(...)    ::mla::Log::Get().Write(::mla::LOG_DEBUG, __VA_ARGS__)
