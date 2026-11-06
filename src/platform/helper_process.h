#pragma once

// A helper program the client talks to over its standard input and output (the browser helper, docs/browser.md):
// started by the client, read without blocking, and stopped with it. Its standard error is the client's.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct SDL_Process;
struct SDL_IOStream;

namespace nmsc::platform {

class HelperProcess {
public:
    // Bytes queued for a helper that is not reading its input (stopped, or its reader stuck): past this it is taken
    // as dead and ended, rather than the queue growing with every message. Two of the protocol's largest messages:
    // BrowserHost holds itself well under it (BrowserHost::kSendBudget), so this ends only a helper gone wrong.
    static constexpr std::size_t kMaxPending = std::size_t{128} << 20;

    explicit HelperProcess(std::size_t max_pending = kMaxPending) : max_pending_(max_pending) {}
    HelperProcess(const HelperProcess&) = delete;
    HelperProcess& operator=(const HelperProcess&) = delete;
    ~HelperProcess(); // stops it

    // args[0] is the program. False (with error set) when it could not be started.
    bool start(const std::vector<std::string>& args, std::string& error);
    // Queues the bytes and writes what the pipe takes now; the rest goes with later writes and flush(). A queue
    // that would pass the most it may hold ends the helper instead (running() is then false).
    void write(std::span<const std::byte> bytes);
    void flush();
    std::size_t pending_write() const { return out_.size(); }
    std::uint64_t written() const { return written_; } // bytes the helper's input has taken in all
    // What has arrived (at most out.size() bytes), without waiting; 0 when nothing has.
    std::size_t read(std::span<std::byte> out);
    // Started and not exited (an exit is noticed here). exit_code() is its code once it has exited.
    bool running();
    int exit_code() const { return exit_code_; }
    // Closes its input (the helper's end of input: its cue to quit) and, when `kill`, ends it at once.
    void stop(bool kill);
    // Waits up to `timeout_ms` for it to exit; true when it has.
    bool wait_exit(int timeout_ms);

private:
    SDL_Process* process_ = nullptr;
    SDL_IOStream* in_ = nullptr; // its standard input
    std::vector<std::byte> out_;
    std::size_t max_pending_;
    std::uint64_t written_ = 0;
    bool exited_ = false;
    int exit_code_ = 0;
};

} // namespace nmsc::platform
