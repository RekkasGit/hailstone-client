#pragma once

// A short report when the client dies: logs/crash-<YYYYMMDD-HHMMSS>.txt (UTC) with the time, what happened and the
// last log lines (log::copy_recent_lines). It is written on a fatal signal (SIGSEGV, SIGBUS, SIGFPE, SIGILL,
// SIGABRT) on POSIX, on an unhandled structured exception on Windows (SetUnhandledExceptionFilter), and on an
// unhandled C++ exception (std::terminate) everywhere. The signal path allocates nothing and uses only
// async-signal-safe calls (open, write, close, time), on its own stack so a stack overflow is reported too (on the
// thread that installed the handlers, and on each thread that holds a CrashStack); after the report the signal's
// earlier action is put back and the signal raised again, so the system still sees the crash and a handler installed
// before ours (AddressSanitizer's) still runs. On Windows the filter that was there before (the CRT's, which turns an
// uncaught C++ exception into std::terminate) is called after ours.

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace hailstone::platform {

// Sets where reports go (a copy of `dir` is kept in a fixed buffer; the folder is created at the report) and
// installs the handlers. Call once, after the log is open.
void install_crash_handler(const std::filesystem::path& dir);
// For a thread the client starts: while it lives, the thread has room to report its own stack overflow (an alternate
// signal stack on POSIX, SetThreadStackGuarantee on Windows). Made first thing in the thread's function.
class CrashStack {
public:
    CrashStack();
    ~CrashStack();
    CrashStack(const CrashStack&) = delete;
    CrashStack& operator=(const CrashStack&) = delete;

private:
    [[maybe_unused]] void* stack_ = nullptr; // POSIX: the alternate stack this made
};

// Only sets the folder, without the handlers (tests).
void set_crash_report_dir(const std::filesystem::path& dir);
// Writes a report now, as the handlers do, saying `what`; false when no folder is set or the file cannot be made.
bool write_crash_report(const char* what);

// The pieces, async-signal-safe: they write into `out` (always NUL-terminated) and return the length.
// "crash-YYYYMMDD-HHMMSS.txt" for `unix_seconds` (UTC).
std::size_t format_crash_name(char* out, std::size_t size, std::int64_t unix_seconds);
// The report: a title, "build: <build>" (when not empty), "time: YYYY-MM-DD HH:MM:SS UTC", "what: <what>",
// "log: <log_path>" (when not empty) and "last log lines:" followed by `recent`. write_crash_report then appends
// the stack ("stack:", one frame a line, glibc's backtrace) where the system has one.
std::size_t format_crash_report(char* out, std::size_t size, std::int64_t unix_seconds, const char* what,
                                const char* log_path, const char* recent, const char* build = nullptr);

} // namespace hailstone::platform
