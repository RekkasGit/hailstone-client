#pragma once

// A new file only its owner may read: a secret key (tools/hailstone_sign), or a folder for one (tools/playprobe).
// Made with that mode from the start, so there is no moment when the umask's wider mode lets another user open it,
// and never over a file already there.

#include <filesystem>
#include <string_view>

namespace nmsc::platform {

// Creates `path` (failing if anything is there already) and writes `text` to it. On POSIX the file is made 0600;
// on Windows it takes its folder's permissions, as any new file there does (a user's own folder is private).
// False when it cannot be made or written; a partly written file is removed.
bool write_private_file(const std::filesystem::path& path, std::string_view text);

// Creates the folder `path` (failing if anything is there already, a link included): 0700 on POSIX; on Windows it
// takes its parent's permissions. For a tool's scratch folder in the shared temp folder that will hold a secret.
bool make_private_dir(const std::filesystem::path& path);

} // namespace nmsc::platform
