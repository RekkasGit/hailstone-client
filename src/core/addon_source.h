#pragma once

// Where an add-on comes from (docs/addons.md): a GitHub repository, and a release tag or a branch of it, as a player
// pastes it on the login screen. A Hailstone feature with no original counterpart. Only GitHub is taken for now; the
// addresses the add-on manager fetches are made here, and a download may only end on one of GitHub's own hosts
// (addon_download_allowed), whatever redirects it took.
//
// Links taken:
//   https://github.com/<owner>/<repo>[.git][/]            the newest release, else the default branch
//   https://github.com/<owner>/<repo>/releases/latest     the same
//   https://github.com/<owner>/<repo>/releases/tag/<tag>  that release, else that tag's files
//   https://github.com/<owner>/<repo>/tree/<branch>       that branch's files
//   github.com/..., www.github.com/... (no scheme), http:// (fetched as https)
//   <owner>/<repo>, <owner>/<repo>@<tag>, <owner>/<repo>#<branch>   (addon_source_text's own form)
// A query or fragment on a link is dropped.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hailstone {

struct AddonSource {
    std::string owner;
    std::string repo;
    std::string tag;    // a release's tag ("" none)
    std::string branch; // a branch ("" none; never with a tag)
};

// The source a link names; nullopt with *error why not, in words for the player.
std::optional<AddonSource> parse_addon_link(std::string_view link, std::string* error = nullptr);

// "owner/repo", "owner/repo@tag" or "owner/repo#branch": what installed.txt keeps, and parse_addon_link takes back.
std::string addon_source_text(const AddonSource& source);
// The repository's page, https://github.com/<owner>/<repo>.
std::string addon_repo_page(const AddonSource& source);

// The addresses of the source's manifest (hailstone-addon.txt), to be tried in order until one answers:
//   no tag or branch: the newest release's asset, then the default branch's file (raw.githubusercontent.com, HEAD)
//   a tag:            that release's asset, then the tag's file
//   a branch:         the branch's file
std::vector<std::string> addon_manifest_urls(const AddonSource& source);

// The manifest's file name.
inline constexpr std::string_view kAddonManifestName = "hailstone-addon.txt";

// Whether an add-on download may go to, or end on, `url`: https on github.com, raw.githubusercontent.com,
// codeload.github.com, objects.githubusercontent.com or release-assets.githubusercontent.com (where GitHub keeps
// release assets and redirects to).
bool addon_download_allowed(std::string_view url);

// A package's address: `file` itself when it is an address, else the file beside the manifest at `manifest_url`.
std::string addon_url_beside(std::string_view manifest_url, std::string_view file);

} // namespace hailstone
