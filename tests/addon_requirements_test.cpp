#include <doctest/doctest.h>

#include "app/addon_requirements.h"
#include "addon_test_util.h"
#include "scratch_dir.h"
#include "ui/dev_screens.h"

using namespace hailstone;
using namespace hailstone::app;

namespace {

plugin::Rules required_rules(std::string identity = "maker/helper") {
    plugin::Rules r;
    r.configured = true;
    r.player_script_caps = plugin::kCapAll;
    r.player_plugin_caps = plugin::kCapAll;
    r.addons.enabled = true;
    r.addons.required.push_back(std::move(identity));
    return r;
}

RequiredAddonCandidate helper() {
    return {"helper", "maker/helper", AddonKind::Script, plugin::kCapRead, true};
}

} // namespace

TEST_CASE("addon requirements: servers must opt in and invalid opted-in policy fails closed") {
    auto r = required_rules();
    r.addons.enabled = false;
    r.addons.error = "unknown field";
    CHECK(addon_requirement_problems(r, {}).empty());
    r.addons.enabled = true;
    auto problems = addon_requirement_problems(r, {});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message.find("invalid") != std::string::npos);
}

TEST_CASE("addon requirements: a repository must match even when another package has its name") {
    auto r = required_rules();
    auto a = helper();
    CHECK(addon_requirement_problems(r, {a}).empty());
    a.source = "other/helper";
    CHECK(addon_requirement_problems(r, {a}).size() == 1);
    r.addons.required = {"HELPER"};
    CHECK(addon_requirement_problems(r, {a}).empty());
}

TEST_CASE("addon requirements: missing modified and non-game packages cannot satisfy a requirement") {
    const auto r = required_rules();
    CHECK(addon_requirement_problems(r, {}).size() == 1);
    auto a = helper();
    a.intact = false;
    auto problems = addon_requirement_problems(r, {a});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message.find("missing or changed") != std::string::npos);
    a.intact = true;
    a.kind = AddonKind::Tool;
    problems = addon_requirement_problems(r, {a});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message.find("separate tool") != std::string::npos);
}

TEST_CASE("addon requirements: blocked identities and incompatible capabilities refuse login") {
    auto r = required_rules();
    const auto a = helper();
    r.addons.block = {"helper"};
    auto problems = addon_requirement_problems(r, {a});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message.find("blocked") != std::string::npos);
    r.addons.block.clear();
    r.player_script_caps = plugin::kCapUi;
    problems = addon_requirement_problems(r, {a});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message.find("capabilities") != std::string::npos);
    r.player_script_caps = 0;
    auto zero = a;
    zero.caps = 0;
    CHECK(addon_requirement_problems(r, {zero}).size() == 1);
}

TEST_CASE("addon requirements: verified server files satisfy names with their own capabilities") {
    auto r = required_rules("helper");
    r.player_script_caps = 0;
    auto a = helper();
    a.source.clear();
    a.server_owned = true;
    CHECK(addon_requirement_problems(r, {a}).empty());
    a.intact = false;
    CHECK(addon_requirement_problems(r, {a}).size() == 1);
    a.intact = true;
    r.addons.required = {"maker/helper"};
    CHECK(addon_requirement_problems(r, {a}).size() == 1);
}

TEST_CASE("addon requirements: native consent remains explicit and refusal prevents joining") {
    auto a = helper();
    a.kind = AddonKind::Plugin;
    const auto r = required_rules();
    CHECK(addon_requirement_problems(r, {a}).empty()); // the existing runtime prompt asks before loading
    a.consent = plugin::Consent::Denied;
    auto problems = addon_requirement_problems(r, {a});
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message.find("not allowed") != std::string::npos);
    a.consent = plugin::Consent::Allowed;
    CHECK(addon_requirement_problems(r, {a}).empty());
}

TEST_CASE("addon requirements: installed upscalers require integrity and defer activation to startup check") {
    auto a = helper();
    a.kind = AddonKind::Upscaler;
    a.caps = 0;
    const auto r = required_rules();
    CHECK(addon_requirement_problems(r, {a}).empty());
    a.intact = false;
    CHECK(addon_requirement_problems(r, {a}).size() == 1);
}

TEST_CASE("addon requirements: required offers keep index identity and key without optional dismissals") {
    AddonIndex index;
    Ed25519PublicKey key{};
    key[0] = 17;
    index.addons.push_back({"helper", "maker/helper", key});
    const auto offers = required_addon_suggestions({{"HELPER", "missing"}, {"maker/helper", "changed"},
                                                   {"other/addon", "missing"}, {"unknown-name", "missing"}}, &index);
    REQUIRE(offers.size() == 2);
    CHECK(offers[0].name == "helper");
    CHECK(offers[0].source == "maker/helper");
    REQUIRE(offers[0].key);
    CHECK(*offers[0].key == key);
    CHECK(offers[0].from_server);
    CHECK(offers[1].source == "other/addon");
    CHECK_FALSE(offers[1].key);
}

TEST_CASE("addon requirements: server entries match mixed-case paths and either slash without changing file case") {
    test::ScratchDir scratch("required_server_case");
    auto rules = required_rules("helper");
    rules.dir = scratch.path();
    rules.addons.required = {"helper", "solo", "native"};
    auto listed = [&](plugin::FileKind kind, const std::string& metadata, const std::string& real,
                       const std::string& bytes) {
        addon_test::put_file(rules.dir / real, bytes);
        rules.server_files.push_back({kind, metadata, sha256(bytes)});
    };
    listed(plugin::FileKind::Script, "ScRiPtS\\HeLpEr\\InIt.LuA", "ScRiPtS/HeLpEr/InIt.LuA", "-- entry");
    listed(plugin::FileKind::Script, "ScRiPtS/HeLpEr/Lib/Helpers.LUA", "ScRiPtS/HeLpEr/Lib/Helpers.LUA", "-- module");
    listed(plugin::FileKind::Script, "ScRiPtS\\Solo.LuA", "ScRiPtS/Solo.LuA", "-- single file");
    listed(plugin::FileKind::Plugin, "PlUgInS\\NaTiVe\\NaTiVe.DlL", "PlUgInS/NaTiVe/NaTiVe.DlL", "native bytes");
    auto candidates = server_addon_requirement_candidates(rules, "windows-x64");
    REQUIRE(candidates.size() == 3);
    CHECK(addon_requirement_problems(rules, candidates).empty());
    for (const auto& candidate : candidates) {
        CHECK(candidate.intact);
        CHECK(candidate.server_owned);
    }
    addon_test::put_file(rules.dir / "ScRiPtS/HeLpEr/Lib/Helpers.LUA", "-- edited");
    const auto problems = addon_requirement_problems(rules, server_addon_requirement_candidates(rules, "windows-x64"));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].identity == "helper");
    CHECK(problems[0].message.find("missing or changed") != std::string::npos);
    CHECK(addon_requirement_problems(rules, server_addon_requirement_candidates(rules, "windows-x64", false)).empty());
}

TEST_CASE("addon requirements: server support files do not replace an entry or the current platform's library") {
    test::ScratchDir scratch("required_server_entry");
    auto rules = required_rules("helper");
    rules.dir = scratch.path();
    rules.addons.required = {"helper", "native"};
    addon_test::put_file(rules.dir / "Scripts/Helper/Util.lua", "-- support");
    addon_test::put_file(rules.dir / "Plugins/Native/Native.so", "other platform");
    rules.server_files = {{plugin::FileKind::Script, "Scripts\\Helper\\Util.lua", sha256("-- support")},
                          {plugin::FileKind::Plugin, "Plugins/Native/Native.so", sha256("other platform")}};
    const auto problems = addon_requirement_problems(rules, server_addon_requirement_candidates(rules, "windows-x64"));
    REQUIRE(problems.size() == 2);
    CHECK(problems[0].identity == "helper");
    CHECK(problems[1].identity == "native");
}

TEST_CASE("addon requirements: server candidate reads reject rooted paths and dot components") {
    test::ScratchDir scratch("required_server_bounds");
    auto rules = required_rules("helper");
    rules.dir = scratch.path() / "profile";
    const std::string entry = "-- valid entry";
    const std::string outside = "-- outside the profile";
    addon_test::put_file(rules.dir / "scripts/helper/init.lua", entry);
    addon_test::put_file(rules.dir / "scripts/helper/module.lua", outside);
    addon_test::put_file(scratch.path() / "outside.lua", outside);
    const plugin::ServerFile valid{plugin::FileKind::Script, "scripts/helper/init.lua", sha256(entry)};
    for (const std::string& path : {std::string("scripts/helper/../../../outside.lua"),
                                   std::string("ScRiPtS\\HeLpEr\\..\\..\\..\\outside.lua"),
                                   std::string("scripts/helper/./module.lua")}) {
        CAPTURE(path);
        rules.server_files = {valid, {plugin::FileKind::Script, path, sha256(outside)}};
        const auto candidates = server_addon_requirement_candidates(rules, "windows-x64");
        REQUIRE(candidates.size() == 1);
        CHECK_FALSE(candidates[0].intact);
        CHECK(addon_requirement_problems(rules, candidates).size() == 1);
        CHECK_FALSE(server_addon_requirement_candidates(rules, "windows-x64", false)[0].intact);
    }
    rules.addons.required = {"native"};
    addon_test::put_file(scratch.path() / "outside/native.dll", outside);
    rules.server_files = {{plugin::FileKind::Plugin, "plugins/../../outside/native.dll", sha256(outside)}};
    CHECK(addon_requirement_problems(rules, server_addon_requirement_candidates(rules, "windows-x64")).size() == 1);
    for (const std::string& path : {std::string("/plugins/native.dll"), std::string("\\plugins\\native.dll"),
                                   std::string("C:\\plugins\\native.dll")}) {
        CAPTURE(path);
        rules.server_files = {{plugin::FileKind::Plugin, path, sha256(outside)}};
        CHECK(server_addon_requirement_candidates(rules, "windows-x64").empty());
        CHECK(addon_requirement_problems(rules, server_addon_requirement_candidates(rules, "windows-x64")).size() == 1);
    }
}

TEST_CASE("addon requirements: presenting a required review fetches nothing before the player's click") {
    test::ScratchDir scratch("required_review");
    addon_test::FakeGitHub github;
    const std::string zip = addon_test::zip_of({{"init.lua", "-- helper"}});
    github.release("maker/helper", addon_test::manifest_for("helper", "script", "1.0.0", "read", "any", zip,
                                                           "helper.zip"), "helper.zip", zip);
    AddonsEnv env;
    env.data_dir = scratch.path();
    env.platform = addon_test::this_platform();
    env.client_version = "1.0.0";
    env.fetch = github.fetch();
    AddonManager manager(std::move(env));
    ui::LoginView view;
    view.busy = true;
    present_addon_requirement(view, {{"maker/helper", "Required add-on is missing."}}, nullptr,
                              "Required add-on is missing.");
    manager.wait_idle();
    CHECK(github.asked.empty());
    CHECK_FALSE(manager.status().offer);
    CHECK_FALSE(view.busy);
    CHECK(view.addons.open);
    CHECK(view.addons.link == "maker/helper");
    CHECK(view.status.is_error);
    // The existing Review action explicitly starts inspection; inspection still does not install a package.
    manager.inspect(view.addons.link);
    manager.wait_idle();
    CHECK_FALSE(github.asked.empty());
    CHECK(manager.status().offer.has_value());
    CHECK(read_installed_addons(scratch.path()).empty());
}
