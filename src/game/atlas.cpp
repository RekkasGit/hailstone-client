#include "game/atlas.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

namespace hailstone::game {

namespace {

// The few TinyXML rules the pages need: elements with attributes (quoted or bare values), children, comments and
// declarations skipped; the documents have several top-level elements.
struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<Node> children;

    const std::string* attr(std::string_view key) const {
        for (const auto& [k, v] : attrs)
            if (k == key) return &v;
        return nullptr;
    }
    // TiXmlElement::QueryIntAttribute: false when missing or not a number.
    bool int_attr(std::string_view key, int& out) const {
        const std::string* v = attr(key);
        return v && std::sscanf(v->c_str(), "%d", &out) == 1;
    }
    const Node* child(std::string_view key) const {
        for (const auto& c : children)
            if (c.name == key) return &c;
        return nullptr;
    }
};

class Reader {
public:
    explicit Reader(std::string_view t) : t_(t) {}

    std::vector<Node> document() {
        std::vector<Node> out;
        elements(out, {});
        return out;
    }

private:
    bool at_end() const { return i_ >= t_.size(); }
    char peek(std::size_t ahead = 0) const { return i_ + ahead < t_.size() ? t_[i_ + ahead] : '\0'; }
    void spaces() {
        while (!at_end() && std::isspace(static_cast<unsigned char>(t_[i_]))) ++i_;
    }
    std::string word() {
        std::size_t s = i_;
        while (!at_end() && (std::isalnum(static_cast<unsigned char>(t_[i_])) || t_[i_] == '_' || t_[i_] == '-' ||
                             t_[i_] == ':' || t_[i_] == '.'))
            ++i_;
        return std::string(t_.substr(s, i_ - s));
    }
    // The elements up to `parent`'s closing tag (or the end).
    void elements(std::vector<Node>& out, std::string_view parent) {
        while (!at_end()) {
            std::size_t lt = t_.find('<', i_);
            if (lt == std::string_view::npos) {
                i_ = t_.size();
                return;
            }
            i_ = lt + 1;
            if (peek() == '/') { // a closing tag: ours ends here
                std::size_t gt = t_.find('>', i_);
                i_ = gt == std::string_view::npos ? t_.size() : gt + 1;
                if (!parent.empty()) return;
                continue;
            }
            if (peek() == '!' || peek() == '?') {
                std::size_t gt = t_.find(peek() == '!' && peek(1) == '-' ? "-->" : ">", i_);
                i_ = gt == std::string_view::npos ? t_.size() : gt + (peek() == '!' && peek(1) == '-' ? 3 : 1);
                continue;
            }
            Node n;
            n.name = word();
            bool open = false;
            while (!at_end()) {
                spaces();
                if (peek() == '/' && peek(1) == '>') {
                    i_ += 2;
                    break;
                }
                if (peek() == '>') {
                    ++i_;
                    open = true;
                    break;
                }
                std::string key = word();
                if (key.empty()) { // something unreadable: skip a character
                    ++i_;
                    continue;
                }
                spaces();
                std::string value;
                if (peek() == '=') {
                    ++i_;
                    spaces();
                    if (peek() == '"' || peek() == '\'') {
                        char q = peek();
                        std::size_t end = t_.find(q, i_ + 1);
                        if (end == std::string_view::npos) end = t_.size();
                        value = std::string(t_.substr(i_ + 1, end - i_ - 1));
                        i_ = std::min(end + 1, t_.size());
                    } else {
                        std::size_t s = i_;
                        while (!at_end() && !std::isspace(static_cast<unsigned char>(t_[i_])) && t_[i_] != '>' &&
                               !(t_[i_] == '/' && peek(1) == '>'))
                            ++i_;
                        value = std::string(t_.substr(s, i_ - s));
                    }
                }
                n.attrs.emplace_back(std::move(key), std::move(value));
            }
            if (open) elements(n.children, n.name);
            out.push_back(std::move(n));
        }
    }

    std::string_view t_;
    std::size_t i_ = 0;
};

// AtlasWnd_ParseRectAttributes @0x61e3f0: CX, CY, X, Y; a missing one adds its name to the error ("Rect(... )").
std::optional<AtlasRect> rect_of(const Node& n, std::string& errors) {
    int cx = 0, cy = 0, x = 0, y = 0;
    std::string missing;
    if (!n.int_attr("CX", cx)) missing += "CX ";
    if (!n.int_attr("CY", cy)) missing += "CY ";
    if (!n.int_attr("X", x)) missing += "X ";
    if (!n.int_attr("Y", y)) missing += "Y ";
    if (!missing.empty()) {
        errors += "Rect(" + missing + ")";
        return std::nullopt;
    }
    return AtlasRect{x, y, x + cx, y + cy};
}

} // namespace

int AtlasPage::width() const {
    int w = 0;
    for (const auto& i : images) w = std::max(w, i.rect.right);
    for (const auto& l : links) w = std::max(w, l.rect.right);
    return w;
}

int AtlasPage::height() const {
    int h = 0;
    for (const auto& i : images) h = std::max(h, i.rect.bottom);
    for (const auto& l : links) h = std::max(h, l.rect.bottom);
    return h;
}

AtlasPage parse_atlas_page(std::string_view text) {
    AtlasPage page;
    std::vector<Node> doc = Reader(text).document();
    auto first = [&](std::string_view name) -> const Node* {
        for (const auto& n : doc)
            if (n.name == name) return &n;
        return nullptr;
    };
    // Background: Image, CX, CY (all of them, or "Background Error(...)").
    if (const Node* b = first("Background")) {
        std::string missing;
        const std::string* image = b->attr("Image");
        int cx = 0, cy = 0;
        if (!image || image->empty()) missing += "Image ";
        if (!b->int_attr("CX", cx)) missing += "CX ";
        if (!b->int_attr("CY", cy)) missing += "CY ";
        if (missing.empty()) {
            page.background = *image;
            page.background_cx = cx;
            page.background_cy = cy;
        } else {
            page.errors += "Background Error(" + missing + ")";
        }
    }
    for (const auto& n : doc) {
        if (n.name == "Image") {
            // AtlasWnd_ParseBackgroundElement @0x61fa30: a Rect child (non-empty) and File.
            std::string err;
            std::optional<AtlasRect> r;
            if (const Node* rc = n.child("Rect")) r = rect_of(*rc, err);
            else err += "Rect ";
            if (r && (r->left > r->right || r->top > r->bottom)) r.reset();
            const std::string* file = n.attr("File");
            if (!file || file->empty()) err += "File ";
            if (r && file && !file->empty()) page.images.push_back({*file, *r});
            else page.errors += "Image Error(" + err + ")";
        } else if (n.name == "Label") {
            // AtlasWnd_ParseLabelElement @0x61f6c0: X, Y, Text; Font (3 when missing).
            AtlasLabel l;
            std::string err;
            if (!n.int_attr("X", l.x)) err += "X ";
            if (!n.int_attr("Y", l.y)) err += "Y ";
            const std::string* t = n.attr("Text");
            if (!t || t->empty()) err += "Text ";
            n.int_attr("Font", l.font);
            if (err.empty()) {
                l.text = *t;
                page.labels.push_back(std::move(l));
            } else {
                page.errors += "Label Error (" + err + ")";
            }
        } else if (n.name == "Link") {
            // AtlasWnd_ParseLinkElement @0x61ff80 / AtlasWnd_CreateLinkButton @0x61fd70.
            AtlasLink k;
            std::string err;
            std::optional<AtlasRect> r;
            if (const Node* rc = n.child("Rect")) r = rect_of(*rc, err);
            else err += "Rect ";
            if (!n.int_attr("ClickAction", k.action)) err += "ClickAction ";
            if (const std::string* f = n.attr("FileToOpen")) k.target = *f;
            if (k.target.empty()) err += "FileToOpen ";
            if (const std::string* v = n.attr("LinkName")) k.name = *v;
            if (const std::string* v = n.attr("ToolTip")) k.tooltip = *v;
            if (const Node* im = n.child("Image")) {
                // AtlasWnd_ParseImageElement @0x61f3f0: CX, CY, File.
                int cx = 0, cy = 0;
                const std::string* file = im->attr("File");
                if (im->int_attr("CX", cx) && im->int_attr("CY", cy) && file && !file->empty())
                    k.image = AtlasImage{*file, {0, 0, cx, cy}};
            }
            if (r && r->left <= r->right && r->top <= r->bottom) {
                k.rect = *r;
                if (k.image) k.image->rect = {r->left, r->top, r->left + k.image->rect.right, r->top + k.image->rect.bottom};
                page.links.push_back(std::move(k));
            }
            if (!err.empty()) page.errors += "Link Error (" + err + ")";
        }
    }
    return page;
}

} // namespace hailstone::game
