#pragma once

// The add-on index (docs/addons.md "Suggested add-ons"): a list, signed with the release key, of the servers that
// have an add-on of their own, so that the login screen can suggest it to a player who logs in to one of them. A
// Hailstone feature with no original counterpart. It is data from its own repository
// (projecthailstone/hailstone-addon-index), not code in the client: the client names no server. Text, one record a
// line, like the update manifest (core/update_manifest.h):
//
//   hailstone-addon-index 1
//   addon <name> <owner/repo> <key>            the add-on: its manifest's name, its GitHub source, the Ed25519 key
//                                              (64 hex digits) its manifest must be signed with
//   world <name> <login server> <world name>   a world it is for: the login server's host ("*" any), and the world's
//                                              name as the login server lists it (the rest of the line, any case)
//   signature <Ed25519 signature, 128 hex digits>   the release key's, over every byte before this line
//
// Unknown records are skipped, for later versions; a record this knows that does not parse refuses the whole index.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ed25519.h"

namespace nmsc {

struct AddonIndexAddon {
    std::string name;   // addon_name_ok
    std::string source; // addon_source_text: owner/repo (no tag or branch)
    Ed25519PublicKey key{};
};

struct AddonIndexWorld {
    std::string addon; // an AddonIndexAddon's name
    std::string login; // the login server's host, lower case; "" any
    std::string world; // as written
};

struct AddonIndex {
    std::vector<AddonIndexAddon> addons;
    std::vector<AddonIndexWorld> worlds;
};

// The index in `text` when its signature is `key`'s and it parses; nullopt with *error why not.
std::optional<AddonIndex> parse_addon_index(std::string_view text, const Ed25519PublicKey& key,
                                            std::string* error = nullptr);

// The index's add-ons for a login to `login_host` whose login server lists `worlds` (each once, in the index's
// order), each with the first of `worlds` it is for. Hosts and world names compare in any case; a world name's outer
// spaces do not count.
struct AddonIndexMatch {
    AddonIndexAddon addon;
    std::string world; // as `worlds` has it
};
std::vector<AddonIndexMatch> addon_index_matches(const AddonIndex& index, std::string_view login_host,
                                                 const std::vector<std::string>& worlds);

// The index's file name (where the client fetches it: app/updater.h default_addon_index()).
inline constexpr std::string_view kAddonIndexName = "hailstone-addon-index.txt";

} // namespace nmsc
