// Suggested add-ons (docs/addons.md "Suggested add-ons"): the add-on index (core/addon_index.h) with its signature,
// matching a login server's worlds, which suggestions the player sees (app/addon_suggest.h), a server's own
// hailstone.cfg [addons] suggest, and the add-on manager fetching and caching the index and holding a suggestion's
// install card to the index's name and key. Malformed and random input is refused without a crash.

#include <doctest/doctest.h>

#include <algorithm>
#include <random>

#include "addon_test_util.h"
#include "app/addon_manager.h"
#include "app/addon_suggest.h"
#include "core/addon_index.h"
#include "core/atomic_file.h"
#include "plugin/policy.h"
#include "scratch_dir.h"

using namespace hailstone;
using namespace hailstone::app;
using namespace hailstone::addon_test;
namespace fs = std::filesystem;

namespace {

const Keys kRelease(9); // the index's signer (the release key)
const Keys kAddonKey(5); // the add-on's own
const std::string kIndexUrl = "https://raw.githubusercontent.com/o/index/HEAD/hailstone-addon-index.txt";

std::string signed_index(const std::string& body, const Keys& key = kRelease) {
    return body + "signature " + hex_encode(ed25519_sign(key.secret, body)) + "\n";
}

std::string index_body(const Ed25519PublicKey& addon_key = kAddonKey.pub) {
    return "hailstone-addon-index 1\n"
           "# a comment\n"
           "addon autoloot owner/autoloot " + hex_encode(addon_key) + "\n"
           "world autoloot login.example.net Loot World\n"
           "world autoloot * Loot World Two\n"
           "future record\n";
}

} // namespace

TEST_CASE("addon_index: a signed index reads") {
    std::string why;
    auto index = parse_addon_index(signed_index(index_body()), kRelease.pub, &why);
    REQUIRE_MESSAGE(index, why);
    REQUIRE(index->addons.size() == 1);
    CHECK(index->addons[0].name == "autoloot");
    CHECK(index->addons[0].source == "owner/autoloot");
    CHECK(index->addons[0].key == kAddonKey.pub);
    REQUIRE(index->worlds.size() == 2);
    CHECK(index->worlds[0].login == "login.example.net");
    CHECK(index->worlds[0].world == "Loot World");
    CHECK(index->worlds[1].login.empty());
    CHECK(index->worlds[1].world == "Loot World Two");
}

TEST_CASE("addon_index: what is refused") {
    std::string why;
    CHECK_FALSE(parse_addon_index(index_body(), kRelease.pub, &why)); // not signed
    CHECK(why.find("not signed") != std::string::npos);
    CHECK_FALSE(parse_addon_index(signed_index(index_body(), Keys(1)), kRelease.pub, &why)); // another key
    CHECK(why.find("release key") != std::string::npos);
    std::string tampered = signed_index(index_body());
    tampered.replace(tampered.find("Loot World"), 4, "Lost");
    CHECK_FALSE(parse_addon_index(tampered, kRelease.pub));
    CHECK_FALSE(parse_addon_index(signed_index(index_body()) + "addon x o/x " + hex_encode(kAddonKey.pub) + "\n",
                                  kRelease.pub)); // after the signature
    const std::string key = hex_encode(kAddonKey.pub);
    for (const std::string& bad : std::vector<std::string>{
             "hailstone-addon-index 2\n",
             "something-else 1\n",
             "hailstone-addon-index 1\naddon bad/name owner/r " + key + "\n",
             "hailstone-addon-index 1\naddon a not-a-link " + key + "\n",
             "hailstone-addon-index 1\naddon a owner/r@v1 " + key + "\n", // a tag: the index names a repository
             "hailstone-addon-index 1\naddon a owner/r 1234\n",
             "hailstone-addon-index 1\naddon a owner/r " + key + " extra\n",
             "hailstone-addon-index 1\naddon a owner/r " + key + "\naddon A owner/s " + key + "\n",
             "hailstone-addon-index 1\nworld a * Some World\n", // an add-on not listed above it
             "hailstone-addon-index 1\naddon a owner/r " + key + "\nworld a bad/host Some World\n",
             "hailstone-addon-index 1\naddon a owner/r " + key + "\nworld a *\n",
             "hailstone-addon-index 1\naddon a owner/r " + key + "\nworld a * " + std::string(65, 'w') + "\n",
         })
        CHECK_MESSAGE(!parse_addon_index(signed_index(bad), kRelease.pub), bad);
    CHECK_FALSE(parse_addon_index(std::string(70 * 1024, '#'), kRelease.pub));
}

TEST_CASE("addon_index: random and cut-off input is refused without a crash") {
    std::mt19937 rng(4321);
    const std::string good = signed_index(index_body());
    for (std::size_t cut = 0; cut < good.size(); ++cut) (void)parse_addon_index(good.substr(0, cut), kRelease.pub);
    for (int i = 0; i < 2000; ++i) {
        std::string s = good;
        for (int j = 0; j < 4; ++j) s[rng() % s.size()] = static_cast<char>(rng() & 0xff);
        (void)parse_addon_index(s, kRelease.pub);
        std::string junk(rng() % 300, '\0');
        for (char& c : junk) c = static_cast<char>(rng() & 0xff);
        (void)parse_addon_index(junk, kRelease.pub);
        (void)parse_dismissed_suggestions(junk);
    }
}

TEST_CASE("addon_index: a login's worlds find their add-ons") {
    const auto index = parse_addon_index(signed_index(index_body()), kRelease.pub);
    REQUIRE(index);
    // the world on its own login server, in any case, with outer spaces; the first listed world it is for
    auto m = addon_index_matches(*index, "Login.Example.NET.", {"Other", " loot world ", "Loot World Two"});
    REQUIRE(m.size() == 1);
    CHECK(m[0].addon.name == "autoloot");
    CHECK(m[0].world == " loot world ");
    // on another login server only the world listed for any ("*")
    CHECK(addon_index_matches(*index, "elsewhere.net", {"Loot World"}).empty());
    m = addon_index_matches(*index, "elsewhere.net", {"Loot World Two"});
    REQUIRE(m.size() == 1);
    CHECK(m[0].world == "Loot World Two");
    // a name that only contains the world's is another world
    CHECK(addon_index_matches(*index, "login.example.net", {"Loot World [Test]", "Loot"}).empty());
}

TEST_CASE("addon_suggest: what the player is offered") {
    const auto index = parse_addon_index(signed_index(index_body()), kRelease.pub);
    REQUIRE(index);
    const std::vector<std::string> worlds{"Loot World"};
    auto s = addon_suggestions(&*index, "login.example.net", worlds, {}, {}, {});
    REQUIRE(s.size() == 1);
    CHECK(s[0].name == "autoloot");
    CHECK(s[0].source == "owner/autoloot");
    CHECK(s[0].world == "Loot World");
    CHECK(s[0].key == kAddonKey.pub);
    CHECK_FALSE(s[0].from_server);
    CHECK(addon_suggestion_text(s[0], "My server") ==
          "Loot World has a Hailstone add-on for its own features: autoloot (owner/autoloot).");

    // not when the player has it: installed from the repository (any tag), by name by hand, or the server's own
    CHECK(addon_suggestions(&*index, "login.example.net", worlds, {}, {{}, {"Owner/AutoLoot@v2"}}, {}).empty());
    CHECK(addon_suggestions(&*index, "login.example.net", worlds, {}, {{"AutoLoot"}, {}}, {}).empty());
    // nor once put away
    CHECK(addon_suggestions(&*index, "login.example.net", worlds, {}, {}, {"owner/autoloot"}).empty());
    // no index, no world: nothing
    CHECK(addon_suggestions(nullptr, "login.example.net", worlds, {}, {}, {}).empty());
    CHECK(addon_suggestions(&*index, "login.example.net", {"Elsewhere"}, {}, {}, {}).empty());

    // the server's own suggestion: by its source; one the index lists keeps the index's name and key, once
    s = addon_suggestions(&*index, "x.net", {}, {"owner/other", "OWNER/autoloot", "owner/other#dev", "bad"}, {}, {});
    REQUIRE(s.size() == 2);
    CHECK(s[0].name.empty());
    CHECK(s[0].source == "owner/other");
    CHECK_FALSE(s[0].key);
    CHECK(s[0].from_server);
    CHECK(addon_suggestion_text(s[0], "My server") == "My server suggests a Hailstone add-on: owner/other.");
    CHECK(s[1].name == "autoloot");
    CHECK(s[1].key == kAddonKey.pub);
    s = addon_suggestions(&*index, "login.example.net", worlds, {"owner/autoloot"}, {}, {});
    REQUIRE(s.size() == 1);
    CHECK_FALSE(s[0].from_server); // the index's, with its world
}

TEST_CASE("addon_suggest: the suggestions put away for good, as hailstone.ini keeps them") {
    CHECK(parse_dismissed_suggestions("Owner/A|owner/b@v1||not a link|owner/a") ==
          std::vector<std::string>{"owner/a", "owner/b"});
    CHECK(join_dismissed_suggestions({"owner/a", "owner/b"}) == "owner/a|owner/b");
    CHECK(parse_dismissed_suggestions("").empty());
    CHECK(addon_repo_key("https://github.com/Owner/Repo/tree/main") == "owner/repo");
    CHECK(addon_repo_key("nope").empty());
}

TEST_CASE("hailstone.cfg: [addons] suggest") {
    std::vector<std::string> warnings;
    auto r = plugin::parse_server_cfg("[addons]\nsuggest = owner/one, https://github.com/owner/two  not/a/link\n",
                                      &warnings);
    CHECK(r.suggest_addons == std::vector<std::string>{"owner/one", "owner/two"});
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("not/a/link") != std::string::npos);
    CHECK(plugin::parse_server_cfg("[scripts]\nplayer = on\n").suggest_addons.empty());
}

TEST_CASE("addon_manager: the add-on index is fetched, cached and checked") {
    test::ScratchDir dir{"addon_index"};
    FakeGitHub github;
    std::int64_t clock = 1'000'000;
    auto start = [&](std::optional<Ed25519PublicKey> key = kRelease.pub) {
        AddonsEnv env;
        env.data_dir = dir.path();
        env.client_version = "0.2.0-alpha";
        env.platform = this_platform();
        env.fetch = github.fetch();
        env.now = [&clock] { return clock; };
        env.index_url = kIndexUrl;
        env.index_key = key;
        auto m = std::make_unique<AddonManager>(std::move(env));
        m->refresh_index(false);
        m->wait_idle();
        return m;
    };
    auto asked = [&] {
        std::lock_guard lock(github.mutex);
        return std::count(github.asked.begin(), github.asked.end(), kIndexUrl);
    };

    SUBCASE("none there: no index, and nothing else goes wrong") {
        auto m = start();
        CHECK_FALSE(m->status().index);
    }
    SUBCASE("fetched once, then from the cache within addons.check_hours") {
        github.files[kIndexUrl] = signed_index(index_body());
        auto m = start();
        REQUIRE(m->status().index);
        CHECK(m->status().index->addons.size() == 1);
        CHECK(asked() == 1);
        m.reset();
        github.files.erase(kIndexUrl);
        m = start();
        CHECK(m->status().index); // the cached one
        CHECK(asked() == 1);
        // a changed cache file is checked again: not used
        write_file_atomic(addons_dir(dir.path()) / "index.txt", index_body() + "signature 00\n");
        m = start();
        CHECK_FALSE(m->status().index);
    }
    SUBCASE("one signed with another key is not used or kept") {
        github.files[kIndexUrl] = signed_index(index_body(), Keys(1));
        auto m = start();
        CHECK_FALSE(m->status().index);
        CHECK_FALSE(fs::exists(addons_dir(dir.path()) / "index.txt"));
    }
    SUBCASE("without a key nothing is fetched") {
        github.files[kIndexUrl] = signed_index(index_body());
        auto m = start(std::nullopt);
        CHECK_FALSE(m->status().index);
        CHECK(asked() == 0);
    }
}

TEST_CASE("addon_manager: a suggestion's install card is held to the index's name and key") {
    test::ScratchDir dir{"addon_index_offer"};
    FakeGitHub github;
    AddonsEnv env;
    env.data_dir = dir.path();
    env.client_version = "0.2.0-alpha";
    env.platform = this_platform();
    env.fetch = github.fetch();
    AddonManager m(std::move(env));
    auto publish = [&](const std::string& name, const Keys* key) {
        const std::string zip = zip_of({{"init.lua", "-- x\n"}});
        std::string manifest = manifest_for(name, "script", "1.0.0", "read", "any", zip, name + ".zip");
        if (key) manifest = key->sign(manifest);
        github.release("owner/autoloot", manifest, name + ".zip", zip);
    };
    auto offer = [&](std::optional<AddonExpect> expect) {
        m.inspect("owner/autoloot", std::move(expect));
        m.wait_idle();
        const AddonsStatus s = m.status();
        REQUIRE_MESSAGE(s.offer, s.message);
        return *s.offer;
    };
    const AddonExpect expect{"autoloot", kAddonKey.pub};

    publish("autoloot", &kAddonKey);
    AddonOffer o = offer(expect);
    CHECK(o.blocked.empty());
    CHECK(o.suggested);
    CHECK_FALSE(offer(std::nullopt).suggested);

    publish("autoloot", nullptr); // unsigned
    CHECK(offer(expect).blocked.find("not signed with the key") != std::string::npos);
    CHECK(offer(std::nullopt).blocked.empty()); // pasted by the player: unsigned is allowed by default
    const Keys other(7);
    publish("autoloot", &other);
    CHECK(offer(expect).blocked.find("not signed with the key") != std::string::npos);
    publish("somethingelse", &kAddonKey);
    CHECK(offer(expect).blocked.find("not the autoloot") != std::string::npos);
}

TEST_CASE("addon_manager: the store lists the index's add-ons with their manifests") {
    test::ScratchDir dir{"addon_index_store"};
    FakeGitHub github;
    AddonsEnv env;
    env.data_dir = dir.path();
    env.client_version = "0.2.0-alpha";
    env.platform = this_platform();
    env.fetch = github.fetch();
    env.index_url = kIndexUrl;
    env.index_key = kRelease.pub;
    const Keys other(7);
    // autoloot (for two worlds), signed with the index's key; hud, signed with another; gone, with no repository;
    // newer, which needs a newer Hailstone
    github.files[kIndexUrl] = signed_index("hailstone-addon-index 1\n"
                                           "addon autoloot owner/autoloot " + hex_encode(kAddonKey.pub) + "\n"
                                           "world autoloot * Loot World\n"
                                           "world autoloot login.example.net Loot World Two\n"
                                           "world autoloot * Loot World\n"
                                           "addon hud owner/hud " + hex_encode(kAddonKey.pub) + "\n"
                                           "addon gone owner/gone " + hex_encode(kAddonKey.pub) + "\n"
                                           "addon newer owner/newer " + hex_encode(kAddonKey.pub) + "\n");
    const std::string zip = zip_of({{"init.lua", "-- x\n"}});
    std::string manifest = manifest_for("autoloot", "script", "1.3.0", "read", "any", zip, "autoloot.zip");
    github.release("owner/autoloot", kAddonKey.sign(manifest), "autoloot.zip", zip);
    github.release("owner/hud", other.sign(manifest_for("hud", "script", "1.0.0", "read", "any", zip, "hud.zip")),
                   "hud.zip", zip);
    const std::string needs =
        manifest_for("newer", "script", "1.0.0", "read", "any", zip, "newer.zip", "min_client 9.0.0\n");
    github.release("owner/newer", kAddonKey.sign(needs), "newer.zip", zip);

    AddonManager m(std::move(env));
    m.refresh_index(true);
    m.browse(false);
    m.wait_idle();
    AddonsStatus s = m.status();
    CHECK(s.browsed);
    CHECK_FALSE(s.browsing);
    CHECK(s.catalog_error.empty());
    REQUIRE(s.catalog.size() == 4);
    const AddonCatalogEntry& loot = s.catalog[0];
    CHECK(loot.addon.name == "autoloot");
    REQUIRE(loot.manifest);
    CHECK(loot.manifest->version_text == "1.3.0");
    CHECK(loot.problem.empty());
    CHECK(loot.worlds == std::vector<std::string>{"Loot World", "Loot World Two"}); // each once, in order
    CHECK_FALSE(s.catalog[1].manifest); // signed with another key than the index names
    CHECK(s.catalog[1].problem.find("not signed with the key") != std::string::npos);
    CHECK_FALSE(s.catalog[2].manifest);
    CHECK_FALSE(s.catalog[2].problem.empty());
    REQUIRE(s.catalog[3].manifest); // shown, but it cannot be installed here
    CHECK(s.catalog[3].problem.find("needs Hailstone 9.0.0") != std::string::npos);

    // once a run, unless asked again
    const std::size_t fetched = github.asked.size();
    m.browse(false);
    m.wait_idle();
    CHECK(github.asked.size() == fetched);
    m.browse(true);
    m.wait_idle();
    CHECK(github.asked.size() > fetched);
}

TEST_CASE("addon_manager: the store without an index says why it is empty") {
    test::ScratchDir dir{"addon_index_store_none"};
    FakeGitHub github;
    AddonsEnv env;
    env.data_dir = dir.path();
    env.client_version = "0.2.0-alpha";
    env.platform = this_platform();
    env.fetch = github.fetch();
    env.index_url = kIndexUrl; // nothing there
    env.index_key = kRelease.pub;
    AddonManager m(std::move(env));
    m.refresh_index(true);
    m.browse(false);
    m.wait_idle();
    const AddonsStatus s = m.status();
    CHECK(s.browsed);
    CHECK(s.catalog.empty());
    CHECK_FALSE(s.catalog_error.empty());
}
