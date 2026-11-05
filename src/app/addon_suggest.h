#pragma once

// Suggested add-ons (docs/addons.md "Suggested add-ons"), a Hailstone feature with no original counterpart: a server
// with an add-on of its own (a compiled plugin with its windows, say) is told apart by Hailstone's add-on index
// (core/addon_index.h: its login server and world name) or by its own hailstone.cfg ([addons] suggest), and the
// login screen offers the add-on to a player who does not have it. Nothing is installed without the player: Look at
// it opens the add-on manager's install card, which holds a suggestion from the index to the index's key.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/addon_index.h"
#include "core/ed25519.h"

namespace nmsc::app {

struct AddonSuggestion {
    std::string name;   // the index's name for it; "" a server's suggestion (only its source is known)
    std::string source; // addon_source_text
    std::string world;  // the world it is for (the index's; "" a server's suggestion)
    std::optional<Ed25519PublicKey> key; // the index's key for it (a server's suggestion: none)
    bool from_server = false; // the server's hailstone.cfg suggests it (and the index does not list it)
};

// What the player has: add-on names (installed, put in by hand, the server's own) and installed sources.
struct AddonsHad {
    std::vector<std::string> names;
    std::vector<std::string> sources;
};

// The suggestions for a login to `login_host` whose login server lists `worlds` (index: may be null) and whose
// hailstone.cfg suggests `server_suggests`, without those the player has or put away (`dismissed`, by
// addon_repo_key). The index's come first; a server's suggestion of an add-on the index lists is the index's.
std::vector<AddonSuggestion> addon_suggestions(const AddonIndex* index, std::string_view login_host,
                                               const std::vector<std::string>& worlds,
                                               const std::vector<std::string>& server_suggests, const AddonsHad& had,
                                               const std::vector<std::string>& dismissed);

// "owner/repo" in lower case (a source's tag or branch dropped): what a suggestion is put away by. "" not a link.
std::string addon_repo_key(std::string_view source);

// The keys put away for good on a server, as hailstone.ini addons.suggest_never.<profile> keeps them ('|' between).
std::vector<std::string> parse_dismissed_suggestions(std::string_view text);
std::string join_dismissed_suggestions(const std::vector<std::string>& keys);

// The sentence the login screen shows for a suggestion; `server`: the picked server's name ("" this server).
std::string addon_suggestion_text(const AddonSuggestion& s, std::string_view server);

} // namespace nmsc::app
