#include "platform/process.h"

#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

#include <SDL3/SDL_platform.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#endif
#endif

namespace hailstone::platform {

unsigned long process_id() {
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

std::optional<std::uint64_t> resident_memory() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return pmc.WorkingSetSize;
    return std::nullopt;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return info.resident_size;
    return std::nullopt;
#elif defined(__linux__)
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return std::nullopt;
    char line[128] = {};
    bool ok = std::fgets(line, sizeof line, f) != nullptr;
    std::fclose(f);
    if (!ok) return std::nullopt;
    // "size resident shared ..." in pages.
    char* end = nullptr;
    std::strtoull(line, &end, 10);
    char* after = nullptr;
    unsigned long long resident = std::strtoull(end, &after, 10);
    if (after == end) return std::nullopt;
    return static_cast<std::uint64_t>(resident) * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#else
    return std::nullopt;
#endif
}

void lower_current_thread_priority() {
#if defined(__linux__)
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}

bool hold_lock_file(const std::filesystem::path& lock) {
#if defined(_WIN32)
    // No sharing: a second open fails while this handle lives; the file goes when the process ends.
    HANDLE h = CreateFileW(lock.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h != INVALID_HANDLE_VALUE) return true; // kept open until the process ends
    // Only another holder makes it taken; any other failure (a read-only folder) is no lock possible, as on POSIX.
    return GetLastError() != ERROR_SHARING_VIOLATION;
#else
    int fd = ::open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return true; // no lock possible (a read-only folder)
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return false;
    }
    return true; // fd kept open: the lock lasts until the process ends
#endif
}

std::FILE* open_c_file(const std::filesystem::path& path, const char* mode) {
#if defined(_WIN32)
    std::wstring wide_mode;
    for (const char* c = mode; *c; ++c) wide_mode += static_cast<wchar_t>(*c);
    return _wfopen(path.c_str(), wide_mode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

std::string system_description() {
    std::string out = SDL_GetPlatform();
#if defined(_WIN32)
    // GetVersionEx answers what the manifest claims; ntdll's RtlGetVersion the real version.
    using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto get = reinterpret_cast<RtlGetVersion>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            OSVERSIONINFOW v{};
            v.dwOSVersionInfoSize = sizeof v;
            if (get(&v) == 0) out += std::format(" {}.{}.{}", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
        }
#else
    if (std::ifstream in("/etc/os-release", std::ios::binary); in) {
        std::stringstream text;
        text << in.rdbuf();
        const std::string release = text.str();
        const std::size_t at = release.find("PRETTY_NAME=");
        if (at != std::string::npos) {
            std::string name = release.substr(at + 12, release.find('\n', at) - at - 12);
            if (name.size() >= 2 && name.front() == '"') name = name.substr(1, name.size() - 2);
            out += ", " + name;
        }
    }
    if (utsname u{}; uname(&u) == 0) out += std::format(" (kernel {}, {})", u.release, u.machine);
    if (const char* session = std::getenv("XDG_SESSION_TYPE")) out += std::string(", ") + session;
#endif
    return out;
}

bool attach_parent_console() {
#if defined(_WIN32)
    if (GetConsoleWindow()) return true; // (a console build has its own)
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return false;
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    return true;
#else
    return true;
#endif
}

} // namespace hailstone::platform
