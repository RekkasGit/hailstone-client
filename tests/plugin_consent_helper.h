#pragma once

// Tests that load a server's compiled plugin (one in servers/<profile>/plugins/, NativePlugins::set_server_dir) give
// the player's yes for its bytes first, as the prompt would (plugin/consent.h): there is no switch that skips it.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/sha256.h"
#include "plugin/consent.h"
#include "plugin/native_plugins.h"

namespace hailstone::test {

// The player allowed (or refused) the library `file` on the server whose folder is `profile_dir`.
inline bool answer_server_plugin(const std::filesystem::path& profile_dir, const std::filesystem::path& file,
                                 bool allowed = true) {
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return plugin::set_plugin_consent(profile_dir, plugin::plugin_name(file), sha256(bytes), allowed);
}

} // namespace hailstone::test
