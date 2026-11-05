#pragma once

// A fresh, empty directory under the system temp folder for one test, removed with the object. The name is unique
// to the process and the call: test runs from several checkouts share the temp folder at once, and a fixed name let
// one run's remove_all() delete another's files mid-test.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace nmsc::test {

inline std::filesystem::path unique_temp_path(const std::string& tag) {
    static const std::uint64_t run = [] {
        std::random_device rd;
        return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    }();
    static std::atomic<unsigned> counter{0};
    return std::filesystem::temp_directory_path() /
           ("nmsc_" + tag + "_" + std::to_string(run) + "_" + std::to_string(counter++));
}

class ScratchDir {
public:
    explicit ScratchDir(const std::string& tag) : path_(unique_temp_path(tag)) {
        std::filesystem::create_directories(path_);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace nmsc::test
