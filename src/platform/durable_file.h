#pragma once

// Writing a file so that it is on the disk, not only in the system's cache, when the call returns: the parts of
// core/atomic_file.h that need the OS. Without them a power cut soon after a save can leave a replaced file empty or
// holding garbage, since the system may write the rename to the disk before the new file's bytes.

#include <filesystem>
#include <string_view>
#include <system_error>

namespace hailstone::platform {

// Creates `path` (replacing a file there) and writes `text` to it, flushed to the disk before the file is closed
// (fsync, F_FULLFSYNC on macOS, FlushFileBuffers on Windows). False when anything fails; the file may then be partly
// written, and the caller removes it.
bool write_file_synced(const std::filesystem::path& path, std::string_view text);

// Renames `from` over `to` (replacing it) and has the rename on the disk before it returns: on POSIX the folder is
// synced after the rename (a failure of that sync is not an error: the rename is done), on Windows the move is made
// with MOVEFILE_WRITE_THROUGH. *ec why it failed.
bool replace_file_synced(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec);

} // namespace hailstone::platform
