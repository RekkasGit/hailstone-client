#pragma once

// Named sections of the main loop's frame, timed for the performance window's hitch log (app/perf_ui.h): what in
// a slow phase took the time. Main thread only.

#include <chrono>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hailstone::core::frame_sections {

using Entry = std::pair<const char*, float>; // a name with static storage, milliseconds

inline std::vector<Entry>& current() {
    static std::vector<Entry> entries;
    return entries;
}

// Adds `ms` to the section `name` (the same name twice in a frame adds up).
inline void add(const char* name, float ms) {
    for (auto& e : current())
        if (e.first == name) {
            e.second += ms;
            return;
        }
    current().emplace_back(name, ms);
}

// A lasting copy of `name`, for a section named at run time (a Lua window's).
inline const char* intern(std::string_view name) {
    static std::set<std::string, std::less<>> names;
    auto it = names.find(name);
    if (it == names.end()) it = names.emplace(name).first;
    return it->c_str();
}

// The frame's sections, emptied for the next frame.
inline std::vector<Entry> take() {
    std::vector<Entry> out;
    out.swap(current());
    return out;
}

// Times a run of code in laps: mark(name) charges the time since the last mark (or the Lap's start) to `name`.
class Lap {
public:
    void mark(const char* name) {
        auto now = std::chrono::steady_clock::now();
        add(name, std::chrono::duration<float, std::milli>(now - last_).count());
        last_ = now;
    }

private:
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};

} // namespace hailstone::core::frame_sections
