#include "app/addon_requirements.h"

#include <algorithm>
#include <optional>

#include "app/update_install.h"
#include "core/binary_header.h"
#include "ui/dev_screens.h"

namespace hailstone::app {

namespace {

std::string path_key(std::string_view path) {
    std::string out(path);
    for (char& c : out) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::optional<std::filesystem::path> listed_path(std::string path) {
    // A Windows separator in metadata denotes the same directory on every host. Preserve the listed case: on
    // case-sensitive filesystems "Scripts/Helper/Init.lua" is not "scripts/helper/init.lua".
    std::replace(path.begin(), path.end(), '\\', '/');
    // Rules can also be supplied directly to this helper, without the server-file downloader's path checks.
    // Reject drive/stream syntax on every host, and never resolve a rooted path or a dot component for a read.
    if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos ||
        path.find('\0') != std::string::npos) return std::nullopt;
    std::filesystem::path relative(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    if (relative.has_root_path()) return std::nullopt;
    for (const auto& part : relative)
        if (part == "." || part == "..") return std::nullopt;
    return relative;
}

} // namespace

std::vector<RequiredAddonCandidate> server_addon_requirement_candidates(
    const plugin::Rules& rules, std::string_view platform, bool verify_files) {
    std::vector<RequiredAddonCandidate> out;
    std::vector<bool> entries;
    if (!rules.addons.enabled || rules.dir.empty()) return out;
    const std::string_view library = plugin_extension(platform);
    for (const auto& f : rules.server_files) {
        const bool script = f.kind == plugin::FileKind::Script;
        const std::string key = path_key(f.path);
        const std::string_view prefix = script ? "scripts/" : "plugins/";
        if (!key.starts_with(prefix)) continue;
        const std::string_view rest = std::string_view(key).substr(prefix.size());
        const auto slash = rest.find('/');
        bool entry = false;
        std::string name;
        if (script) {
            if (slash == std::string_view::npos) {
                if (!rest.ends_with(".lua")) continue;
                name = rest.substr(0, rest.size() - 4);
                entry = true;
            } else {
                name = rest.substr(0, slash);
                entry = rest.substr(slash + 1) == "init.lua";
            }
            if (name == "config") continue;
        } else {
            const auto last = rest.rfind('/');
            const auto filename = rest.substr(last == std::string_view::npos ? 0 : last + 1);
            if (library.empty() || !filename.ends_with(library)) continue;
            name = filename.substr(0, filename.size() - library.size());
            if (name.size() > 3 && name.starts_with("lib")) name.erase(0, 3);
            entry = true;
        }
        if (!addon_name_ok(name) || !plugin::addon_required(rules.addons, name)) continue;
        const AddonKind kind = script ? AddonKind::Script : AddonKind::Plugin;
        auto it = std::find_if(out.begin(), out.end(), [&](const RequiredAddonCandidate& a) {
            return a.kind == kind && a.name == name;
        });
        if (it == out.end()) {
            out.push_back({name, {}, kind, plugin::kCapAll, true, true,
                           script ? plugin::Consent::Unasked : plugin::Consent::Allowed});
            entries.push_back(false);
            it = out.end() - 1;
        }
        const auto index = static_cast<std::size_t>(it - out.begin());
        const auto relative = listed_path(f.path);
        if (!relative) {
            it->intact = false;
            continue;
        }
        entries[index] = entries[index] || entry;
        std::error_code ec;
        const auto file = rules.dir / *relative;
        const auto size = std::filesystem::file_size(file, ec);
        it->intact = it->intact && !ec && (!verify_files || package_file_matches(file, size, f.sha256));
        if (!script) {
            const auto consent = plugin::plugin_consent(rules.dir, f.sha256);
            if (consent == plugin::Consent::Denied) it->consent = consent;
            else if (consent == plugin::Consent::Unasked && it->consent == plugin::Consent::Allowed)
                it->consent = consent;
        }
    }
    for (std::size_t i = 0; i < out.size(); ++i) out[i].intact = out[i].intact && entries[i];
    return out;
}

std::vector<AddonRequirementProblem> addon_requirement_problems(
    const plugin::Rules& rules, const std::vector<RequiredAddonCandidate>& candidates) {
    std::vector<AddonRequirementProblem> out;
    if (!rules.addons.enabled) return out;
    if (!rules.addons.error.empty()) {
        out.push_back({{}, "This server's add-on policy is invalid: " + rules.addons.error});
        return out;
    }
    for (const auto& required : rules.addons.required) {
        std::string problem = "Required add-on " + required + " is not installed. Open Add-ons to install it.";
        bool found = false;
        for (const auto& a : candidates) {
            if (!plugin::addon_identity_matches(required, a.name, a.source)) continue;
            const auto allowed_caps = a.kind == AddonKind::Script ? rules.player_script_caps : rules.player_plugin_caps;
            if (!plugin::addon_allowed(rules.addons, a.name, a.source)) {
                problem = "Required add-on " + required + " is blocked by this server's add-on policy.";
            } else if (a.kind == AddonKind::Tool) {
                problem = "Required add-on " + required + " is a separate tool and cannot run as a game add-on.";
            } else if (!a.intact) {
                problem = "Required add-on " + required + " has missing or changed files. Reinstall it in Add-ons.";
            } else if (!a.server_owned && (a.kind == AddonKind::Script || a.kind == AddonKind::Plugin) &&
                       (a.caps == 0 || (a.caps & ~allowed_caps))) {
                problem = "Required add-on " + required + " needs capabilities this server does not allow.";
            } else if (a.kind == AddonKind::Plugin && a.consent == plugin::Consent::Denied) {
                problem = "Required plugin " + required + " was not allowed to run. Open Add-ons to review your answer.";
            } else {
                found = true;
                break;
            }
        }
        if (!found) out.push_back({required, std::move(problem)});
    }
    return out;
}

std::vector<AddonSuggestion> required_addon_suggestions(
    const std::vector<AddonRequirementProblem>& problems, const AddonIndex* index) {
    std::vector<AddonSuggestion> out;
    for (const auto& p : problems) {
        if (p.identity.empty()) continue;
        const AddonIndexAddon* listed = nullptr;
        if (index)
            for (const auto& a : index->addons)
                if (plugin::addon_identity_matches(p.identity, a.name, a.source)) { listed = &a; break; }
        AddonSuggestion sg;
        sg.from_server = true;
        if (listed) {
            sg.name = listed->name;
            sg.source = listed->source;
            sg.key = listed->key;
        } else {
            sg.source = addon_repo_key(p.identity);
        }
        if (sg.source.empty()) continue;
        const bool seen = std::any_of(out.begin(), out.end(), [&](const AddonSuggestion& a) {
            return addon_repo_key(a.source) == addon_repo_key(sg.source);
        });
        if (!seen) out.push_back(std::move(sg));
    }
    return out;
}

void present_addon_requirement(ui::LoginView& view, const std::vector<AddonRequirementProblem>& problems,
                               const AddonIndex* index, std::string message) {
    view.busy = false;
    view.status = {message, true};
    view.addons.open = true;
    view.addons.status = {std::move(message), true};
    const auto offers = required_addon_suggestions(problems, index);
    if (!offers.empty()) view.addons.link = offers.front().source;
}

} // namespace hailstone::app
