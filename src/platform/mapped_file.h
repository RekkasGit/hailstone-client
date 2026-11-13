#pragma once

// A read-only mapping of a file: assets::PfsArchive's whole-file storage (src/assets/pfs.h). The view is the file
// itself, not a heap copy: the bytes live in the system's file cache, shared with other readers and with other
// Hailstone processes mapping the same files, and they can leave the resident set under memory pressure instead of
// sitting in private heap for the process's life. wld::WldFile data parsed from those bytes is still private.

#include <cstddef>
#include <filesystem>
#include <span>

namespace hailstone::platform {

// A read-only mapped file. Move-only, like a handle: copies would each map the file again. The view stays valid
// until the mapping is unmapped (moved-from or destroyed); concurrent reads of the view are safe.
//
// Replacement: on POSIX, renaming a new file over the path leaves the view showing the old bytes until it is
// unmapped, and a fresh open sees the new file. On Windows a rename over a file with a mapped view is typically refused (the
// sharing flags below let the file be opened and deleted, not replaced while a section exists), so map only files
// nothing replaces while they are mapped: assets::PfsArchive::storage_for maps the game folder's files and reads the
// server overlay's (which the server files sync replaces) into memory.
//
// The underlying file must not be edited or truncated in place while mapped: a read of a page past the new end faults
// (SIGBUS on POSIX, EXCEPTION_IN_PAGE_ERROR on Windows), and an edit shows through. See docs/hardening.md.
//
// OS calls live here only; callers see a span. The Windows and POSIX spellings differ in their .cpp, nothing else.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // Maps `path` read-only. Throws std::runtime_error when it cannot be opened or mapped. An empty file gives an
    // empty (unmapped) result, as a zero-length read did before.
    static MappedFile open(const std::filesystem::path& path);

    // True when a view is held (an empty file is not mapped). size() is the view's length either way.
    bool mapped() const noexcept { return view_ != nullptr; }
    std::size_t size() const noexcept { return size_; }
    std::span<const std::byte> view() const noexcept {
        return std::span<const std::byte>(view_bytes_, size_);
    }

private:
    void reset() noexcept;

    const std::byte* view_bytes_ = nullptr;
    void* view_ = nullptr;   // the OS view (mmap / MapViewOfFile); POSIX and Windows put it here
    std::size_t size_ = 0;
    void* file_ = nullptr;   // Windows: the file handle (POSIX closes its fd after mmap)
    void* mapping_ = nullptr; // Windows: the file mapping (POSIX: none)
};

} // namespace hailstone::platform
