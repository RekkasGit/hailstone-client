// ClientApp: the in-game browser (docs/browser.md). The helper (browser/hailstone-web beside the binary) starts with
// the first tab; its data is data_dir()/web/, one profile folder per saved server.

#include "app/client_app_internal.h"
#include "app/lua_eq.h"
#include "platform/open_folder.h"

namespace nmsc::app::client_internal {

web::BrowserHost* ClientApp::browser() {
    if (browser_ || browser_tried_) return browser_.get();
    browser_tried_ = true;
#if defined(_WIN32)
    constexpr const char* kHelper = "hailstone-web.exe";
#else
    constexpr const char* kHelper = "hailstone-web";
#endif
    const std::filesystem::path helper = platform::install_dir() / "browser" / kHelper;
    std::error_code ec;
    if (!std::filesystem::exists(helper, ec)) {
        log::info("web: no {}: web pages open in the system's browser", helper.string());
        return nullptr;
    }
    const std::filesystem::path data = platform::data_dir() / "web";
    std::filesystem::create_directories(data, ec);
    // hailstone.ini browser.sandbox: auto (the helper decides: docs/browser.md), on, off.
    std::string sandbox = config_.get_string("browser.sandbox", "auto");
    if (sandbox != "on" && sandbox != "off") sandbox = "auto";
    browser_ = std::make_unique<web::BrowserHost>([helper, data, sandbox] {
        return web::process_transport({helper.string(), "--data=" + data.string(), "--sandbox=" + sandbox});
    });
    web_textures_ = std::make_unique<ui::WebTextures>(vk_);
    return browser_.get();
}

void ClientApp::close_orphan_tabs() {
    if (!browser_ || !web_service_ || !automation_) return;
    std::vector<std::string> gone;
    for (std::uint32_t id : browser_->tabs())
        if (const web::Tab* t = browser_->tab(id); t && !automation_->web_owner_alive(t->options.owner))
            gone.push_back(t->options.owner);
    for (const std::string& owner : gone) web_service_->close_owner(owner);
    web_service_->recheck([this](const std::string& owner) { return automation_->plugin_caps(owner); });
}

bool ClientApp::open_web_page(const std::string& url) {
    if (web_service_ && web_service_->available() && eq_api_) {
        std::string why;
        if (web_service_->open("", plugin::kCapAll, url, 800, 600, &why)) {
            eq_api_->show("BrowserWnd", true);
            return true;
        }
        log::warn("web: {}", why);
    }
    platform::open_url(url);
    return false;
}

} // namespace nmsc::app::client_internal
