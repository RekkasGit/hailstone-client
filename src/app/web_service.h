#pragma once

// The in-game browser for the UI packs, plugins and scripts (plugin/web_access.h, docs/browser.md) over the app's
// web::BrowserHost: who may do what with which tab, and the page's view in an ImGui window (its texture, the mouse
// and keyboard it takes).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "plugin/web_access.h"

namespace nmsc::web { class BrowserHost; }
namespace nmsc::ui { class WebTextures; }

namespace nmsc::app {

struct WebServiceEnv {
    std::function<web::BrowserHost*()> browser;   // made on first use; null without the helper
    std::function<ui::WebTextures*()> textures;   // the tabs' textures (null: the page is drawn blank)
    std::function<std::string()> profile;         // the player's cookie folder for this server (owner_profile)
    std::function<std::vector<std::string>()> allow; // the server's [web] allow (empty: any host)
    std::function<void()> wants_text;             // a page has the keyboard this frame: text input on
};

class WebService final : public plugin::WebAccess {
public:
    // The most tabs one script or plugin holds at once, those the helper or the rules closed included (they stay,
    // with their queued messages, until it closes them): each is a browser in the helper and up to a 64 MB image
    // here. The player's own tabs are not limited.
    static constexpr std::size_t kMaxTabsPerOwner = 16;

    explicit WebService(WebServiceEnv env) : env_(std::move(env)) {}

    bool available() override;
    std::uint32_t open(const std::string& owner, std::uint32_t caps, const std::string& url, int width, int height,
                       std::string* why = nullptr) override;
    bool navigate(const std::string& owner, std::uint32_t caps, std::uint32_t tab, const std::string& url,
                  std::string* why = nullptr) override;
    bool nav(const std::string& owner, std::uint32_t caps, std::uint32_t tab, const std::string& action) override;
    void close(const std::string& owner, std::uint32_t tab) override;
    bool eval(const std::string& owner, std::uint32_t caps, std::uint32_t tab, const std::string& script) override;
    bool post(const std::string& owner, std::uint32_t caps, std::uint32_t tab, const std::string& message) override;
    bool text(const std::string& owner, std::uint32_t caps, std::uint32_t tab, const std::string& text) override;
    std::optional<std::string> take_message(const std::string& owner, std::uint32_t tab) override;
    std::optional<plugin::WebState> state(const std::string& owner, std::uint32_t tab) override;
    std::vector<std::uint32_t> tabs(const std::string& owner) override;
    bool view(const std::string& owner, std::uint32_t caps, std::uint32_t tab, float width, float height) override;
    void close_owner(const std::string& owner) override;

    // What the player typed into an address bar: a bare host or path gets https://.
    static std::string address(std::string text);
    // The cookie and cache folder of `owner`'s tabs: the server's folder `base` for the player's own, a folder of its
    // own below it for each script and plugin ("hailstone-addons/<name>-<hash>"), so no add-on's page shares the
    // player's cookies (signed-in sessions) or another add-on's.
    static std::string owner_profile(const std::string& base, const std::string& owner);

private:
    web::BrowserHost* host();
    // The caller's own live tab, and whether it may act (kCapWeb unless the player).
    bool mine(const std::string& owner, std::uint32_t tab);
    static bool may(const std::string& owner, std::uint32_t caps, std::string* why = nullptr);

    WebServiceEnv env_;
    std::uint32_t focused_ = 0; // the tab that has the keyboard (clicked last)
    std::uint32_t hovered_ = 0; // the tab the mouse is over (it is told when it leaves)
    float last_x_ = -1, last_y_ = -1;
};

} // namespace nmsc::app
