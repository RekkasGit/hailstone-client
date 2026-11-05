#include "platform/http_get.h"

#include <algorithm>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <vector>
#else
#include <SDL3/SDL_loadso.h>
#include <mutex>
#endif

namespace nmsc::platform {

namespace {

// "http" or "https" for an address this fetches; "" for any other.
std::string_view scheme_of(const std::string& url) {
    auto starts = [&](std::string_view p) {
        if (url.size() <= p.size()) return false;
        for (std::size_t i = 0; i < p.size(); ++i) {
            char c = url[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != p[i]) return false;
        }
        return true;
    };
    if (starts("https://")) return "https";
    if (starts("http://")) return "http";
    return {};
}

bool is_2xx(int status) { return status >= 200 && status < 300; }

std::string status_error(int status) { return "the server answered HTTP " + std::to_string(status); }

} // namespace

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string win_error(const char* what) {
    return std::string(what) + " failed (WinHTTP error " + std::to_string(GetLastError()) + ")";
}

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET v) : h(v) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

} // namespace

bool http_available(std::string* why) {
    if (why) why->clear();
    return true;
}

HttpGetResult http_get(const std::string& url, const HttpGetOptions& options,
                       const std::function<bool(std::span<const std::byte>)>& sink,
                       const std::function<bool(std::uint64_t, std::uint64_t)>& progress) {
    HttpGetResult r;
    const std::string_view scheme = scheme_of(url);
    if (scheme.empty()) {
        r.error = "not an http or https address";
        return r;
    }
    std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (wurl.empty() || !WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &parts)) {
        r.error = "not a valid address";
        return r;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    const bool secure = scheme == "https";

    const std::wstring agent = widen(options.user_agent);
    // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY (the system's proxy settings) is Windows 8.1 and later.
    Handle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) {
        session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session.h) {
        r.error = win_error("WinHttpOpen");
        return r;
    }
    const int connect_ms = std::max(1, options.connect_timeout_s) * 1000;
    const int stall_ms = std::max(1, options.stall_timeout_s) * 1000;
    WinHttpSetTimeouts(session.h, connect_ms, connect_ms, stall_ms, stall_ms);
    DWORD redirects = 10;
    WinHttpSetOption(session.h, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects, sizeof(redirects));

    Handle connection(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
    if (!connection.h) {
        r.error = win_error("WinHttpConnect");
        return r;
    }
    Handle request(WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request.h) {
        r.error = win_error("WinHttpOpenRequest");
        return r;
    }
    // WinHTTP follows redirects itself and by default never from https to http (WINHTTP_OPTION_REDIRECT_POLICY).
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        r.error = win_error("WinHttpSendRequest");
        return r;
    }
    if (!WinHttpReceiveResponse(request.h, nullptr)) {
        r.error = win_error("WinHttpReceiveResponse");
        return r;
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        r.error = win_error("WinHttpQueryHeaders");
        return r;
    }
    r.status = static_cast<int>(status);
    if (!is_2xx(r.status)) {
        r.error = status_error(r.status);
        return r;
    }
    if (options.allow_url) {
        // The address the redirects ended on.
        DWORD url_size = 0;
        WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, nullptr, &url_size);
        std::wstring final_url(url_size / sizeof(wchar_t) + 1, L'\0');
        url_size = static_cast<DWORD>(final_url.size() * sizeof(wchar_t));
        std::string narrow;
        if (WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, final_url.data(), &url_size)) {
            final_url.resize(url_size / sizeof(wchar_t));
            int n = WideCharToMultiByte(CP_UTF8, 0, final_url.data(), static_cast<int>(final_url.size()), nullptr, 0,
                                        nullptr, nullptr);
            if (n > 0) {
                narrow.resize(static_cast<std::size_t>(n));
                WideCharToMultiByte(CP_UTF8, 0, final_url.data(), static_cast<int>(final_url.size()), narrow.data(), n,
                                    nullptr, nullptr);
            }
        }
        if (narrow.empty() || !options.allow_url(narrow)) {
            r.error = "the download was sent to an address it may not come from";
            return r;
        }
    }
    // The length is a 64-bit number as text (the numeric query is only 32 bits).
    std::uint64_t total = 0;
    wchar_t length[32] = {};
    DWORD length_size = sizeof(length);
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length,
                            &length_size, WINHTTP_NO_HEADER_INDEX)) {
        for (const wchar_t* p = length; *p >= L'0' && *p <= L'9'; ++p)
            total = total * 10 + static_cast<std::uint64_t>(*p - L'0');
    }
    if (options.max_bytes && total > options.max_bytes) {
        r.error = "the answer is larger than allowed";
        return r;
    }

    std::vector<std::byte> buffer(64 * 1024);
    std::uint64_t received = 0;
    if (progress && !progress(0, total)) {
        r.error = "cancelled";
        return r;
    }
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
            r.error = win_error("WinHttpReadData");
            return r;
        }
        if (got == 0) break;
        received += got;
        if (options.max_bytes && received > options.max_bytes) {
            r.error = "the answer is larger than allowed";
            return r;
        }
        if (!sink(std::span<const std::byte>(buffer.data(), got)) || (progress && !progress(received, total))) {
            r.error = "cancelled";
            return r;
        }
    }
    if (total && received != total) {
        r.error = "the answer ended early";
        return r;
    }
    r.ok = true;
    return r;
}

#else // libcurl

namespace {

// libcurl's API, declared here as the library is loaded at run time. Every value is from curl/curl.h (libcurl
// 8.23.0): an option is its type base (CURLOPTTYPE_LONG 0, OBJECTPOINT 10000, FUNCTIONPOINT 20000) plus its number.
using CurlHandle = void;
constexpr int kOptWriteData = 10001;         // CURLOPT_WRITEDATA (CBPOINT, 1)
constexpr int kOptUrl = 10002;               // CURLOPT_URL (STRINGPOINT, 2)
constexpr int kOptWriteFunction = 20011;     // CURLOPT_WRITEFUNCTION (FUNCTIONPOINT, 11)
constexpr int kOptUserAgent = 10018;         // CURLOPT_USERAGENT (STRINGPOINT, 18)
constexpr int kOptLowSpeedLimit = 19;        // CURLOPT_LOW_SPEED_LIMIT (LONG, 19)
constexpr int kOptLowSpeedTime = 20;         // CURLOPT_LOW_SPEED_TIME (LONG, 20)
constexpr int kOptNoProgress = 43;           // CURLOPT_NOPROGRESS (LONG, 43)
constexpr int kOptFollowLocation = 52;       // CURLOPT_FOLLOWLOCATION (LONG, 52)
constexpr int kOptXferInfoData = 10057;      // CURLOPT_XFERINFODATA (CBPOINT, 57)
constexpr int kOptMaxRedirs = 68;            // CURLOPT_MAXREDIRS (LONG, 68)
constexpr int kOptConnectTimeout = 78;       // CURLOPT_CONNECTTIMEOUT (LONG, 78)
constexpr int kOptNoSignal = 99;            // CURLOPT_NOSIGNAL (LONG, 99)
constexpr int kOptProtocols = 181;           // CURLOPT_PROTOCOLS (LONG, 181; deprecated by PROTOCOLS_STR)
constexpr int kOptRedirProtocols = 182;      // CURLOPT_REDIR_PROTOCOLS (LONG, 182)
constexpr int kOptXferInfoFunction = 20219;  // CURLOPT_XFERINFOFUNCTION (FUNCTIONPOINT, 219)
constexpr int kOptProtocolsStr = 10318;      // CURLOPT_PROTOCOLS_STR (STRINGPOINT, 318; libcurl 7.85.0)
constexpr int kOptRedirProtocolsStr = 10319; // CURLOPT_REDIR_PROTOCOLS_STR (STRINGPOINT, 319)
constexpr int kInfoResponseCode = 0x200002;  // CURLINFO_RESPONSE_CODE (CURLINFO_LONG 0x200000 + 2)
constexpr int kInfoEffectiveUrl = 0x100001;  // CURLINFO_EFFECTIVE_URL (CURLINFO_STRING 0x100000 + 1)
constexpr long kProtoHttp = 1L << 0;         // CURLPROTO_HTTP
constexpr long kProtoHttps = 1L << 1;        // CURLPROTO_HTTPS
constexpr long kGlobalDefault = 3;           // CURL_GLOBAL_DEFAULT (CURL_GLOBAL_SSL | CURL_GLOBAL_WINSOCK)
constexpr int kOk = 0;                       // CURLE_OK
constexpr int kWriteError = 23;              // CURLE_WRITE_ERROR
constexpr int kAbortedByCallback = 42;       // CURLE_ABORTED_BY_CALLBACK

using WriteFn = std::size_t (*)(char*, std::size_t, std::size_t, void*);
using XferInfoFn = int (*)(void*, std::int64_t, std::int64_t, std::int64_t, std::int64_t); // curl_off_t

struct Curl {
    SDL_SharedObject* lib = nullptr;
    std::string why;
    int (*global_init)(long) = nullptr;
    CurlHandle* (*easy_init)() = nullptr;
    int (*easy_setopt)(CurlHandle*, int, ...) = nullptr;
    int (*easy_perform)(CurlHandle*) = nullptr;
    int (*easy_getinfo)(CurlHandle*, int, ...) = nullptr;
    void (*easy_cleanup)(CurlHandle*) = nullptr;
    const char* (*easy_strerror)(int) = nullptr;
};

template <typename F> bool load(SDL_SharedObject* lib, const char* name, F& out) {
    out = reinterpret_cast<F>(SDL_LoadFunction(lib, name));
    return out != nullptr;
}

const Curl& curl() {
    static Curl c;
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef __APPLE__
        const char* names[] = {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib"};
#else
        const char* names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"};
#endif
        for (const char* name : names) {
            c.lib = SDL_LoadObject(name);
            if (c.lib) break;
        }
        if (!c.lib) {
            c.why = "libcurl is not installed (looked for " + std::string(names[0]) + ")";
            return;
        }
        if (!load(c.lib, "curl_global_init", c.global_init) || !load(c.lib, "curl_easy_init", c.easy_init) ||
            !load(c.lib, "curl_easy_setopt", c.easy_setopt) || !load(c.lib, "curl_easy_perform", c.easy_perform) ||
            !load(c.lib, "curl_easy_getinfo", c.easy_getinfo) ||
            !load(c.lib, "curl_easy_cleanup", c.easy_cleanup) ||
            !load(c.lib, "curl_easy_strerror", c.easy_strerror)) {
            c.why = "the system's libcurl lacks a function Hailstone needs";
            SDL_UnloadObject(c.lib);
            c.lib = nullptr;
            return;
        }
        if (c.global_init(kGlobalDefault) != kOk) {
            c.why = "libcurl could not start";
            SDL_UnloadObject(c.lib);
            c.lib = nullptr;
        }
    });
    return c;
}

struct Transfer {
    const Curl* c = nullptr;
    CurlHandle* h = nullptr;
    const HttpGetOptions* options = nullptr;
    const std::function<bool(std::span<const std::byte>)>* sink = nullptr;
    const std::function<bool(std::uint64_t, std::uint64_t)>* progress = nullptr;
    std::uint64_t received = 0;
    bool cancelled = false;
    bool too_large = false;
    bool bad_status = false;
    bool url_checked = false;
    bool refused_url = false;
};

std::size_t on_write(char* data, std::size_t size, std::size_t count, void* user) {
    auto& t = *static_cast<Transfer*>(user);
    const std::size_t n = size * count;
    // A redirect's own body never reaches here; any other answer that is not 2xx stops the transfer.
    long status = 0;
    t.c->easy_getinfo(t.h, kInfoResponseCode, &status);
    if (!is_2xx(static_cast<int>(status))) {
        t.bad_status = true;
        return 0;
    }
    if (t.options->allow_url && !t.url_checked) {
        // The address the redirects ended on, before any of its body goes on.
        t.url_checked = true;
        char* url = nullptr;
        t.c->easy_getinfo(t.h, kInfoEffectiveUrl, &url);
        if (!url || !t.options->allow_url(std::string_view(url))) {
            t.refused_url = true;
            return 0;
        }
    }
    t.received += n;
    if (t.options->max_bytes && t.received > t.options->max_bytes) {
        t.too_large = true;
        return 0;
    }
    if (!(*t.sink)(std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), n))) {
        t.cancelled = true;
        return 0;
    }
    return n;
}

int on_progress(void* user, std::int64_t total, std::int64_t now, std::int64_t, std::int64_t) {
    auto& t = *static_cast<Transfer*>(user);
    if (t.options->max_bytes && total > 0 && static_cast<std::uint64_t>(total) > t.options->max_bytes) {
        t.too_large = true;
        return 1;
    }
    if (t.progress && *t.progress) {
        if (!(*t.progress)(static_cast<std::uint64_t>(std::max<std::int64_t>(now, 0)),
                           static_cast<std::uint64_t>(std::max<std::int64_t>(total, 0)))) {
            t.cancelled = true;
            return 1;
        }
    }
    return 0;
}

} // namespace

bool http_available(std::string* why) {
    const Curl& c = curl();
    if (why) *why = c.lib ? std::string() : c.why;
    return c.lib != nullptr;
}

HttpGetResult http_get(const std::string& url, const HttpGetOptions& options,
                       const std::function<bool(std::span<const std::byte>)>& sink,
                       const std::function<bool(std::uint64_t, std::uint64_t)>& progress) {
    HttpGetResult r;
    const std::string_view scheme = scheme_of(url);
    if (scheme.empty()) {
        r.error = "not an http or https address";
        return r;
    }
    const Curl& c = curl();
    if (!c.lib) {
        r.error = c.why;
        return r;
    }
    CurlHandle* h = c.easy_init();
    if (!h) {
        r.error = "libcurl could not start a transfer";
        return r;
    }
    Transfer t;
    t.c = &c;
    t.h = h;
    t.options = &options;
    t.sink = &sink;
    t.progress = &progress;
    const bool secure = scheme == "https";
    const WriteFn write_fn = on_write;
    const XferInfoFn progress_fn = on_progress;

    c.easy_setopt(h, kOptUrl, url.c_str());
    c.easy_setopt(h, kOptUserAgent, options.user_agent.c_str());
    c.easy_setopt(h, kOptWriteFunction, write_fn);
    c.easy_setopt(h, kOptWriteData, static_cast<void*>(&t));
    c.easy_setopt(h, kOptXferInfoFunction, progress_fn);
    c.easy_setopt(h, kOptXferInfoData, static_cast<void*>(&t));
    c.easy_setopt(h, kOptNoProgress, 0L);
    // Downloads run on several threads at once: without this libcurl may use SIGALRM for the resolver timeout,
    // which is not thread safe (the libcurl docs, CURLOPT_NOSIGNAL).
    c.easy_setopt(h, kOptNoSignal, 1L);
    c.easy_setopt(h, kOptFollowLocation, 1L);
    c.easy_setopt(h, kOptMaxRedirs, 10L);
    c.easy_setopt(h, kOptConnectTimeout, static_cast<long>(std::max(1, options.connect_timeout_s)));
    c.easy_setopt(h, kOptLowSpeedLimit, 1L);
    c.easy_setopt(h, kOptLowSpeedTime, static_cast<long>(std::max(1, options.stall_timeout_s)));
    // Only http and https, and once on https never back to http. The _STR options are libcurl 7.85.0 and later.
    const char* redirect = secure ? "https" : "http,https";
    if (c.easy_setopt(h, kOptProtocolsStr, "http,https") != kOk ||
        c.easy_setopt(h, kOptRedirProtocolsStr, redirect) != kOk) {
        c.easy_setopt(h, kOptProtocols, kProtoHttp | kProtoHttps);
        c.easy_setopt(h, kOptRedirProtocols, secure ? kProtoHttps : kProtoHttp | kProtoHttps);
    }

    const int code = c.easy_perform(h);
    long status = 0;
    c.easy_getinfo(h, kInfoResponseCode, &status);
    r.status = static_cast<int>(status);
    if (t.refused_url) {
        r.error = "the download was sent to an address it may not come from";
    } else if (t.too_large) {
        r.error = "the answer is larger than allowed";
    } else if (t.cancelled) {
        r.error = "cancelled";
    } else if (t.bad_status || (code == kOk && !is_2xx(r.status))) {
        r.error = status_error(r.status);
    } else if (code != kOk) {
        const char* text = c.easy_strerror(code);
        r.error = text ? text : "libcurl error " + std::to_string(code);
        if (code == kWriteError || code == kAbortedByCallback) r.error = "cancelled";
    } else {
        r.ok = true;
    }
    c.easy_cleanup(h);
    return r;
}

#endif

} // namespace nmsc::platform
