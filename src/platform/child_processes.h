#pragma once

// Other programs this one starts and leaves running (the box set's other Hailstone windows): started detached
// from this one's console, never stopped by it, and collected once they exit so none is left a zombie.

#include <string>
#include <vector>

struct SDL_Process;

namespace hailstone::platform {

class ChildProcesses {
public:
    ChildProcesses() = default;
    ChildProcesses(const ChildProcesses&) = delete;
    ChildProcesses& operator=(const ChildProcesses&) = delete;
    ~ChildProcesses(); // forgets them: they go on running

    // args[0] is the program. False (with error set) when it could not be started.
    bool start(const std::vector<std::string>& args, std::string& error);
    // Collects the ones that have exited.
    void poll();
    std::size_t running() const { return children_.size(); }

private:
    std::vector<SDL_Process*> children_;
};

} // namespace hailstone::platform
