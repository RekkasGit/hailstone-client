#include "core/addon_source.h"

#include <array>

namespace nmsc {

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool istarts(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (lower(s[i]) != prefix[i]) return false;
    return true;
}

bool alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// GitHub's own rules: an owner is 1 to 39 letters, digits and single hyphens, not at either end.
bool owner_ok(std::string_view s) {
    if (s.empty() || s.size() > 39 || s.front() == '-' || s.back() == '-') return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!alnum(s[i]) && s[i] != '-') return false;
        if (s[i] == '-' && i > 0 && s[i - 1] == '-') return false;
    }
    return true;
}

// A repository is 1 to 100 letters, digits, '.', '-' and '_', and not "." or "..".
bool repo_ok(std::string_view s) {
    if (s.empty() || s.size() > 100 || s == "." || s == "..") return false;
    for (char c : s)
        if (!alnum(c) && c != '.' && c != '-' && c != '_') return false;
    return true;
}

// A tag or a branch, as it goes into an address unescaped: letters, digits, '.', '-', '_' and '/' between parts; no
// ".." part, nothing that starts a part with '.' or the whole with '-'.
bool ref_ok(std::string_view s) {
    if (s.empty() || s.size() > 100 || s.front() == '-' || s.front() == '/' || s.back() == '/') return false;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.find('/', start);
        if (end == std::string_view::npos) end = s.size();
        const std::string_view part = s.substr(start, end - start);
        if (part.empty() || part.front() == '.') return false;
        for (char c : part)
            if (!alnum(c) && c != '.' && c != '-' && c != '_') return false;
        start = end + 1;
    }
    return true;
}

std::nullopt_t fail(std::string* error, std::string why) {
    if (error) *error = std::move(why);
    return std::nullopt;
}

std::vector<std::string_view> parts_of(std::string_view path) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start < path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        if (end > start) out.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::string joined(const std::vector<std::string_view>& parts, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < parts.size(); ++i) {
        if (!out.empty()) out += '/';
        out += parts[i];
    }
    return out;
}

// The host of an https address, lower-case ("" when it is not https or has user info or a port).
std::string https_host(std::string_view url) {
    if (!istarts(url, "https://")) return {};
    std::string_view rest = url.substr(8);
    const std::size_t end = rest.find_first_of("/?#");
    std::string_view host = rest.substr(0, end);
    if (host.empty() || host.find_first_of("@:") != std::string_view::npos) return {};
    std::string out;
    for (char c : host) out += lower(c);
    return out;
}

} // namespace

std::optional<AddonSource> parse_addon_link(std::string_view link, std::string* error) {
    while (!link.empty() && (link.front() == ' ' || link.front() == '\t')) link.remove_prefix(1);
    while (!link.empty() && (link.back() == ' ' || link.back() == '\t' || link.back() == '\r' || link.back() == '\n'))
        link.remove_suffix(1);
    if (link.empty()) return fail(error, "paste a GitHub link to the add-on");
    if (link.size() > 400) return fail(error, "that link is too long");
    for (char c : link)
        if (static_cast<unsigned char>(c) <= ' ' || c == 0x7f || c == '\\')
            return fail(error, "that is not a GitHub link");

    AddonSource s;
    const bool url = istarts(link, "https://") || istarts(link, "http://") || istarts(link, "github.com/") ||
                     istarts(link, "www.github.com/");
    if (url) {
        std::string_view rest = link;
        if (istarts(rest, "https://")) rest.remove_prefix(8);
        else if (istarts(rest, "http://")) rest.remove_prefix(7);
        if (istarts(rest, "www.")) rest.remove_prefix(4);
        if (!istarts(rest, "github.com/")) return fail(error, "only GitHub links are taken for now (github.com/...)");
        rest.remove_prefix(11);
        rest = rest.substr(0, rest.find_first_of("?#"));
        const auto parts = parts_of(rest);
        if (parts.size() < 2) return fail(error, "a GitHub link to an add-on names its owner and repository");
        s.owner = std::string(parts[0]);
        std::string_view repo = parts[1];
        if (repo.size() > 4 && istarts(repo.substr(repo.size() - 4), ".git")) repo.remove_suffix(4);
        s.repo = std::string(repo);
        const bool newest = parts.size() == 2 || (parts[2] == "releases" && parts.size() == 3) ||
                            (parts[2] == "releases" && parts.size() == 4 && parts[3] == "latest");
        if (parts.size() > 2 && parts[2] == "releases" && parts.size() >= 5 && parts[3] == "tag")
            s.tag = joined(parts, 4);
        else if (parts.size() > 2 && parts[2] == "tree" && parts.size() >= 4)
            s.branch = joined(parts, 3);
        else if (!newest)
            return fail(error, "link to the repository, a release or a branch of it");
    } else {
        // owner/repo, owner/repo@tag, owner/repo#branch
        std::string_view rest = link;
        const std::size_t at = rest.find_first_of("@#");
        if (at != std::string_view::npos) {
            (rest[at] == '@' ? s.tag : s.branch) = std::string(rest.substr(at + 1));
            rest = rest.substr(0, at);
        }
        const std::size_t slash = rest.find('/');
        if (slash == std::string_view::npos || rest.find('/', slash + 1) != std::string_view::npos)
            return fail(error, "paste a GitHub link (https://github.com/owner/repo) or owner/repo");
        s.owner = std::string(rest.substr(0, slash));
        s.repo = std::string(rest.substr(slash + 1));
        if ((at != std::string_view::npos) && s.tag.empty() && s.branch.empty())
            return fail(error, "name the tag after '@', or the branch after '#'");
    }
    if (!owner_ok(s.owner)) return fail(error, "\"" + s.owner + "\" is not a GitHub owner");
    if (!repo_ok(s.repo)) return fail(error, "\"" + s.repo + "\" is not a GitHub repository");
    if (!s.tag.empty() && !ref_ok(s.tag)) return fail(error, "\"" + s.tag + "\" is not a tag this takes");
    if (!s.branch.empty() && !ref_ok(s.branch)) return fail(error, "\"" + s.branch + "\" is not a branch this takes");
    return s;
}

std::string addon_source_text(const AddonSource& s) {
    std::string out = s.owner + "/" + s.repo;
    if (!s.tag.empty()) out += "@" + s.tag;
    else if (!s.branch.empty()) out += "#" + s.branch;
    return out;
}

std::string addon_repo_page(const AddonSource& s) { return "https://github.com/" + s.owner + "/" + s.repo; }

std::vector<std::string> addon_manifest_urls(const AddonSource& s) {
    const std::string name(kAddonManifestName);
    const std::string raw = "https://raw.githubusercontent.com/" + s.owner + "/" + s.repo + "/";
    const std::string releases = addon_repo_page(s) + "/releases/";
    if (!s.branch.empty()) return {raw + s.branch + "/" + name};
    if (!s.tag.empty()) return {releases + "download/" + s.tag + "/" + name, raw + s.tag + "/" + name};
    return {releases + "latest/download/" + name, raw + "HEAD/" + name};
}

bool addon_download_allowed(std::string_view url) {
    static constexpr std::array<std::string_view, 5> kHosts = {
        "github.com", "raw.githubusercontent.com", "codeload.github.com", "objects.githubusercontent.com",
        "release-assets.githubusercontent.com"};
    const std::string host = https_host(url);
    for (std::string_view h : kHosts)
        if (host == h) return true;
    return false;
}

std::string addon_url_beside(std::string_view manifest_url, std::string_view file) {
    if (istarts(file, "https://") || istarts(file, "http://")) return std::string(file);
    // The manifest's own address without its query, up to its last '/'.
    std::string_view base = manifest_url.substr(0, manifest_url.find_first_of("?#"));
    const std::size_t slash = base.rfind('/');
    return std::string(slash == std::string_view::npos ? std::string_view() : base.substr(0, slash + 1)) +
           std::string(file);
}

} // namespace nmsc
