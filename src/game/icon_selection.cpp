#include "game/icon_selection.h"

#include <format>

namespace hailstone::game {

namespace {

constexpr std::int32_t kPerPageChoices[] = {50, 100, 500, 1000, -1}; // ISW_PerPage_Combobox: 50 .. All

} // namespace

const char* icon_type_animation(int type) {
    switch (type) {
    case 0: return "A_DragItem";
    case 1: return "A_SpellGems";
    case 2: return "A_MenuIcons";
    case 3: return "A_SpeakingIndicator";
    default: return nullptr;
    }
}

IconCacheRange icon_cache_range(int type, std::optional<std::int32_t> cells) {
    IconCacheRange r;
    if (type == 0) r.base = r.min = 500;
    static constexpr std::int32_t kMissing[] = {4999, 299, 0x23, 2}; // FindAnimation1 found nothing
    r.max = cells ? *cells + r.base : kMissing[type & 3];
    return r;
}

std::int32_t grid_cells(int frame_w, int frame_h, int cell_w, int cell_h, int frames) {
    if (cell_w <= 0 || cell_h <= 0) return 0;
    return (frame_w / cell_w) * (frame_h / cell_h) * frames;
}

const IconCacheRange* IconSelection::range() const {
    if (type_ < 0 || type_ >= kIconTypes || !ranges[type_]) return nullptr; // __GetAnimationCache: none
    return &*ranges[type_];
}

bool IconSelection::open(std::int32_t type, std::int32_t selected) {
    offset_ = 0;
    return set_type(type, selected);
}

bool IconSelection::set_type(std::int32_t type, std::int32_t selected) {
    type_ = type < 0 ? 1 : type; // a type above 0x7fffffff (unsigned) is 1
    if (!range()) return false;
    set_selected(selected);
    update_range();
    return true;
}

void IconSelection::set_selected(std::int32_t icon) {
    const IconCacheRange* r = range();
    if (!r) return;
    selected_ = icon < r->min ? r->min : icon > r->max ? r->max : icon;
}

void IconSelection::update_range() {
    const IconCacheRange* r = range();
    if (!r) return;
    if (offset_ < 0) {
        std::int32_t last = r->max - r->min - 1;
        offset_ = per_page_ > 0 ? last - last % per_page_ : 0;
    } else if (r->max - r->min <= offset_) {
        offset_ = 0;
    }
}

void IconSelection::choose_type(int choice) {
    offset_ = 0;
    set_type(choice, -1);
}

void IconSelection::choose_per_page(int choice) {
    if (choice < 0 || choice > 4) return;
    per_page_ = kPerPageChoices[choice];
    offset_ = 0;
    set_type(type_, -1);
}

int IconSelection::per_page_choice() const {
    for (int i = 0; i < 5; ++i)
        if (kPerPageChoices[i] == per_page_) return i;
    return 0;
}

void IconSelection::page_left() {
    if (per_page_ < 1) return;
    offset_ -= per_page_;
    set_type(type_, selected_);
}

void IconSelection::page_right() {
    if (per_page_ < 1) return;
    offset_ += per_page_;
    set_type(type_, selected_);
}

std::int32_t IconSelection::first() const {
    const IconCacheRange* r = range();
    return r ? r->min + offset_ : 0;
}

std::int32_t IconSelection::last() const {
    const IconCacheRange* r = range();
    if (!r) return -1;
    std::int32_t start = r->min + offset_;
    if (per_page_ != -1 && start + per_page_ <= r->max) return start + per_page_ - 1;
    return r->max - 1;
}

int IconSelection::page_number() const { return per_page_ == -1 ? 1 : offset_ / per_page_ + 1; }

std::string IconSelection::range_text() const {
    const IconCacheRange* r = range();
    if (!r) return {};
    std::int32_t start = r->min + offset_, end = r->max;
    if (per_page_ != -1 && start + per_page_ <= end) end = start + per_page_;
    return std::format("Icons: {} - {}", start, end - 1);
}

} // namespace hailstone::game
