#include "ui/font_faces.h"

#include "platform/paths.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <system_error>

namespace nmsc::ui {

namespace {

std::uint16_t u16(std::span<const unsigned char> d, std::size_t at) {
    return at + 2 <= d.size() ? static_cast<std::uint16_t>(d[at] << 8 | d[at + 1]) : 0;
}
std::uint32_t u32(std::span<const unsigned char> d, std::size_t at) {
    return at + 4 <= d.size() ? static_cast<std::uint32_t>(d[at]) << 24 | static_cast<std::uint32_t>(d[at + 1]) << 16 |
                                    static_cast<std::uint32_t>(d[at + 2]) << 8 | d[at + 3]
                              : 0;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// UTF-16BE (the Windows and Unicode platforms' names) to UTF-8.
std::string utf16be(std::span<const unsigned char> d) {
    std::string out;
    for (std::size_t i = 0; i + 1 < d.size(); i += 2) {
        std::uint32_t c = static_cast<std::uint32_t>(d[i] << 8 | d[i + 1]);
        if (c >= 0xd800 && c < 0xe000) {
            // A surrogate: only a high one followed by a low one is a character;
            // an unpaired half becomes U+FFFD and the next unit is read as itself.
            std::uint32_t lo = i + 3 < d.size() ? static_cast<std::uint32_t>(d[i + 2] << 8 | d[i + 3]) : 0;
            if (c < 0xdc00 && lo >= 0xdc00 && lo < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                i += 2;
            } else {
                c = 0xfffd;
            }
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | c >> 6);
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | c >> 12);
            out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | c >> 18);
            out += static_cast<char>(0x80 | (c >> 12 & 0x3f));
            out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

bool font_file_name(const std::filesystem::path& p) {
    const std::u8string u = p.extension().u8string(); // (string() throws on Windows for a name the code page lacks)
    std::string ext = lower(std::string(u.begin(), u.end()));
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc";
}

void style_flags(FontFace& f) {
    std::string s = lower(f.style);
    f.italic = s.find("italic") != std::string::npos || s.find("oblique") != std::string::npos;
    f.bold = s.find("bold") != std::string::npos && s.find("semi") == std::string::npos &&
             s.find("demi") == std::string::npos;
}

} // namespace

std::optional<FontFace> parse_font_face(std::span<const unsigned char> d) {
    std::size_t font = 0;
    if (u32(d, 0) == 0x74746366) font = u32(d, 12); // 'ttcf': the first font
    const std::uint32_t version = u32(d, font);
    if (version != 0x00010000 && version != 0x4f54544f && version != 0x74727565) return std::nullopt; // 1.0, OTTO, true
    const std::uint16_t tables = u16(d, font + 4);
    std::size_t name = 0, name_size = 0;
    for (std::uint16_t i = 0; i < tables; ++i) {
        std::size_t rec = font + 12 + std::size_t{i} * 16;
        if (u32(d, rec) == 0x6e616d65) { // 'name'
            name = u32(d, rec + 8);
            name_size = u32(d, rec + 12);
        }
    }
    if (name == 0 || name + name_size > d.size() || name_size < 6) return std::nullopt;
    const std::uint16_t count = u16(d, name + 2);
    const std::size_t strings = name + u16(d, name + 4);
    // Per name id the best string: the Windows platform's US English, then another Windows language, then the Mac's
    // Roman (rank 3 .. 1).
    std::map<std::uint16_t, std::pair<int, std::string>> best;
    for (std::uint16_t i = 0; i < count; ++i) {
        std::size_t rec = name + 6 + std::size_t{i} * 12;
        if (rec + 12 > name + name_size) break;
        const std::uint16_t platform = u16(d, rec), encoding = u16(d, rec + 2), language = u16(d, rec + 4);
        const std::uint16_t id = u16(d, rec + 6), length = u16(d, rec + 8), offset = u16(d, rec + 10);
        if (id != 1 && id != 2 && id != 16 && id != 17) continue;
        const std::size_t at = strings + offset;
        if (at + length > d.size()) continue;
        auto bytes = d.subspan(at, length);
        int rank = 0;
        std::string text;
        if (platform == 3 && (encoding == 1 || encoding == 10)) {
            rank = language == 0x409 ? 3 : 2;
            text = utf16be(bytes);
        } else if (platform == 0) {
            rank = 2;
            text = utf16be(bytes);
        } else if (platform == 1 && encoding == 0) {
            rank = 1;
            text.assign(bytes.begin(), bytes.end());
        }
        if (rank > 0 && !text.empty() && rank > best[id].first) best[id] = {rank, text};
    }
    FontFace f;
    f.family = !best[16].second.empty() ? best[16].second : best[1].second;
    f.style = !best[17].second.empty() ? best[17].second : best[2].second;
    if (f.family.empty()) return std::nullopt;
    if (f.style.empty()) f.style = "Regular";
    style_flags(f);
    return f;
}

std::optional<FontFace> read_font_face(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    // The table directory, then only the name table (font files run to megabytes).
    std::vector<unsigned char> head(4096);
    in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(in.gcount()));
    std::span<const unsigned char> h(head);
    std::size_t font = u32(h, 0) == 0x74746366 ? u32(h, 12) : 0;
    if (font + 12 > head.size()) return std::nullopt;
    const std::uint16_t tables = u16(h, font + 4);
    for (std::uint16_t i = 0; i < tables && font + 12 + std::size_t{i} * 16 + 16 <= head.size(); ++i) {
        std::size_t rec = font + 12 + std::size_t{i} * 16;
        if (u32(h, rec) != 0x6e616d65) continue;
        const std::uint32_t offset = u32(h, rec + 8), length = u32(h, rec + 12);
        if (length > (1u << 20)) return std::nullopt;
        // A small image of the file: the directory as it is, then the name table, its record pointed there. (Not at
        // its own offset: that is the file's word, up to 4 GB, and a broken font would have it all zero-filled.)
        const std::size_t at = head.size();
        std::vector<unsigned char> image(at + length, 0);
        std::copy(head.begin(), head.end(), image.begin());
        for (std::size_t k = 0; k < 4; ++k) image[rec + 8 + k] = static_cast<unsigned char>(at >> (24 - 8 * k));
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset));
        if (!in) return std::nullopt;
        in.read(reinterpret_cast<char*>(image.data() + at), static_cast<std::streamsize>(length));
        if (static_cast<std::uint32_t>(in.gcount()) != length) return std::nullopt;
        auto face = parse_font_face(image);
        if (face) face->file = file;
        return face;
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> system_font_dirs() {
    std::vector<std::filesystem::path> dirs;
    const auto env = platform::env_path; // wide-aware on Windows
#if defined(_WIN32)
    std::filesystem::path windows = env("WINDIR");
    dirs.push_back((windows.empty() ? std::filesystem::path("C:/Windows") : windows) / "Fonts");
    if (auto local = env("LOCALAPPDATA"); !local.empty()) dirs.push_back(local / "Microsoft/Windows/Fonts");
#elif defined(__APPLE__)
    dirs = {"/System/Library/Fonts", "/Library/Fonts"};
    if (auto home = env("HOME"); !home.empty()) dirs.push_back(home / "Library/Fonts");
#else
    std::filesystem::path data_home = env("XDG_DATA_HOME");
    if (data_home.empty())
        if (auto home = env("HOME"); !home.empty()) data_home = home / ".local/share";
    if (!data_home.empty()) dirs.push_back(data_home / "fonts");
    if (auto home = env("HOME"); !home.empty()) dirs.push_back(home / ".fonts");
    std::string data_dirs = env("XDG_DATA_DIRS").string();
    if (data_dirs.empty()) data_dirs = "/usr/local/share:/usr/share";
    for (std::size_t at = 0; at <= data_dirs.size();) {
        std::size_t colon = data_dirs.find(':', at);
        if (colon == std::string::npos) colon = data_dirs.size();
        if (colon > at) dirs.push_back(std::filesystem::path(data_dirs.substr(at, colon - at)) / "fonts");
        at = colon + 1;
    }
#endif
    return dirs;
}

std::vector<FontFace> scan_font_faces(std::span<const std::filesystem::path> dirs) {
    std::vector<FontFace> faces;
    std::error_code ec;
    for (const auto& dir : dirs) {
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 dir, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec) || !font_file_name(it->path())) continue;
            if (auto f = read_font_face(it->path())) faces.push_back(std::move(*f));
        }
        ec.clear();
    }
    std::sort(faces.begin(), faces.end(), [](const FontFace& a, const FontFace& b) {
        std::string fa = lower(a.family), fb = lower(b.family);
        if (fa != fb) return fa < fb;
        return lower(a.style) < lower(b.style);
    });
    return faces;
}

std::vector<std::string> font_families(std::span<const FontFace> faces) {
    std::vector<std::string> out;
    for (const auto& f : faces)
        if (out.empty() || lower(out.back()) != lower(f.family)) out.push_back(f.family);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    out.erase(std::unique(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) == lower(b); }),
              out.end());
    return out;
}

FontFiles find_font_family(std::span<const FontFace> faces, std::string_view family) {
    FontFiles out;
    const std::string want = lower(family);
    int regular_rank = 0, bold_rank = 0;
    for (const auto& f : faces) {
        if (lower(f.family) != want || f.italic) continue;
        const std::string style = lower(f.style);
        if (!f.bold) {
            int rank = style == "regular" || style == "normal" || style == "book" || style == "roman" ? 2 : 1;
            if (rank > regular_rank) {
                regular_rank = rank;
                out.regular = f.file;
            }
        } else {
            int rank = style == "bold" ? 2 : 1;
            if (rank > bold_rank) {
                bold_rank = rank;
                out.bold = f.file;
            }
        }
    }
    return out;
}

} // namespace nmsc::ui
