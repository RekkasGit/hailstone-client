#include "game/real_estate.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace nmsc::game {

using net::ReType;

void RealEstateManager::set_catalog(net::ReCatalog catalog) {
    // ReadDefsFromPacket clears the catalog first (FUN_007f5a50), ReadCostsFromPacket the cost groups.
    catalog_ = std::move(catalog);
    // DefinitionTypeOf follows the catalog: the root's type is recomputed for the new definitions.
    if (const net::ReRecord* r = record(occ_.root)) occ_.root_type = definition_type(r->definition);
}

const net::ReDefinition* RealEstateManager::definition(std::uint32_t id) const {
    for (const auto& d : catalog_.definitions)
        if (d.id == id) return &d;
    return nullptr;
}

ReType RealEstateManager::definition_type(std::uint32_t id) const {
    const net::ReDefinition* d = definition(id);
    return d ? d->type : ReType::Unknown;
}

const net::ReCostSet* RealEstateManager::cost_set(std::uint32_t id) const {
    for (const auto& c : catalog_.costs)
        if (c.id == id) return &c;
    return nullptr;
}

void RealEstateManager::add_record(net::ReRecord record) {
    auto id = record.id;
    records_[id] = std::move(record);
}

void RealEstateManager::set_current(net::ReRecord record) {
    // 0x6fd0: mgr +0x8c = the id, +0x90 = its definition's type, then the same add as 0x63fd.
    occ_.root = record.id;
    occ_.root_type = definition_type(record.definition);
    add_record(std::move(record));
}

void RealEstateManager::remove_record(std::uint32_t id) { records_.erase(id); }

void RealEstateManager::merge_search_record(const net::ReRecord& r) {
    // FUN_005b0f80 form B: an id the client has gets only its owner (FUN_005b4fe0) and guild (vt+8); others are
    // made by RealEstate::RealEstate @0x7fe1e0 from {id, 0, parent, definition, guild, f28, address, owner}.
    auto it = records_.find(r.id);
    if (it != records_.end()) {
        it->second.owner = r.owner;
        it->second.guild_id = r.guild_id;
        return;
    }
    net::ReRecord fresh;
    fresh.id = r.id;
    fresh.parent = r.parent;
    fresh.definition = r.definition;
    fresh.guild_id = r.guild_id;
    fresh.f28 = r.f28;
    fresh.address = r.address;
    fresh.owner = r.owner;
    records_[r.id] = std::move(fresh);
}

const net::ReRecord* RealEstateManager::record(std::uint32_t id) const {
    auto it = records_.find(id);
    return it == records_.end() ? nullptr : &it->second;
}

ReType RealEstateManager::record_type(std::uint32_t id) const {
    const net::ReRecord* r = record(id);
    return r ? definition_type(r->definition) : ReType::Unknown;
}

bool RealEstateManager::already_owned(std::uint32_t id) const {
    const net::ReRecord* r = record(id);
    if (!r) return false;
    ReType t = definition_type(r->definition);
    if (t == ReType::Plot) return !r->owner.empty();
    if (t == ReType::GuildPlot) return r->guild_id != -1;
    return false;
}

const net::ReAccess* RealEstateManager::access(std::uint32_t re) const {
    auto it = access_.find(re);
    return it == access_.end() ? nullptr : &it->second;
}

void RealEstateManager::add_item_rows(const net::ReItemList& list) {
    // 0x087f finds or creates the summary and names it; 0x1cef / 0x2cf7 append (a row whose re / item pair is known
    // replaces it, as the windows' OnRealEstateItemUpdated do).
    ReItemSummary& s = summaries_[list.re];
    if (list.name) s.name = *list.name;
    for (const auto& row : list.rows) {
        auto it = std::find_if(s.rows.begin(), s.rows.end(),
                               [&](const net::ReItemRow& r) { return r.re == row.re && r.item_id == row.item_id; });
        if (it != s.rows.end()) *it = row;
        else s.rows.push_back(row);
    }
}

void RealEstateManager::rename_summary(std::uint32_t re, std::string name) {
    auto it = summaries_.find(re);
    if (it != summaries_.end()) it->second.name = std::move(name);
}

void RealEstateManager::remove_item(std::uint32_t re, std::uint32_t item_id) {
    auto it = summaries_.find(re);
    if (it == summaries_.end()) return;
    auto& rows = it->second.rows;
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const net::ReItemRow& r) { return r.item_id == item_id; }),
               rows.end());
}

const ReItemSummary* RealEstateManager::summary(std::uint32_t re) const {
    auto it = summaries_.find(re);
    return it == summaries_.end() ? nullptr : &it->second;
}

void RealEstateManager::set_owned(OwnedList list, const std::vector<std::uint32_t>& ids) {
    // FUN_005b4060 / 40a0 / 40e0 clear the list, then each id is added once (PcBase's add-unique).
    auto& v = owned_[static_cast<int>(list)];
    v.clear();
    for (auto id : ids)
        if (std::find(v.begin(), v.end(), id) == v.end()) v.push_back(id);
}

void RealEstateManager::toggle_owned(OwnedList list, std::uint32_t id, bool add) {
    auto& v = owned_[static_cast<int>(list)];
    auto it = std::find(v.begin(), v.end(), id);
    if (add && it == v.end()) v.push_back(id);
    if (!add && it != v.end()) v.erase(it);
}

bool RealEstateManager::point_in_area(const net::ReArea& area, const glm::vec3& p) {
    // FUN_00505ba0 inverts the area's 4×4 (row-major as sent: m[12..14] the translation); PointInVolume then takes
    // the point as a row vector: local = p · inverse.
    glm::mat4 m;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) m[r][c] = area.m[static_cast<std::size_t>(r * 4 + c)];
    // glm's m[r] is a column; filling m[r][c] with the row-major element (r, c) gives the transpose, whose column
    // vector product is the original's row-vector product.
    glm::mat4 inv = glm::inverse(m);
    glm::vec4 local = inv * glm::vec4(p, 1.0f);
    return std::fabs(local.x) <= 1.0f && std::fabs(local.y) <= 1.0f && std::fabs(local.z) <= 1.0f;
}

bool RealEstateManager::add_placed(const net::RePlacedItem& item) {
    for (auto& p : placed_)
        if (p.re == item.re && p.item_id == item.item_id) {
            p = item; // EQPlacedItem_CopyFrom: the data only
            return false;
        }
    placed_.push_back(item);
    return true;
}

void RealEstateManager::remove_placed(std::uint32_t re, std::uint32_t item_id) {
    placed_.erase(std::remove_if(placed_.begin(), placed_.end(),
                                 [&](const net::RePlacedItem& p) { return p.re == re && p.item_id == item_id; }),
                  placed_.end());
}

const net::RePlacedItem* RealEstateManager::placed(std::uint32_t re, std::uint32_t item_id) const {
    for (const auto& p : placed_)
        if (p.re == re && p.item_id == item_id) return &p;
    return nullptr;
}

bool RealEstateManager::descends_from(std::uint32_t id, std::uint32_t ancestor) const {
    // FUN_004b3090(id, ancestor, -1, 1): id itself or a parent up the chain, at most 10 steps from depth 1.
    for (int depth = 1; depth <= 10; ++depth) {
        if (id == ancestor) return true;
        const net::ReRecord* r = record(id);
        if (!r) return false;
        id = r->parent;
    }
    return false;
}

std::uint32_t RealEstateManager::area_estate(std::uint32_t area_id, std::uint32_t root) const {
    // FUN_004b30f0: a record whose definition's f01 is the area's id, and that is the root or below it (the test
    // starts from the record's parent).
    for (const auto& [id, r] : records_) {
        const net::ReDefinition* d = definition(r.definition);
        std::int32_t link = d ? d->f01 : -1;
        if (link != static_cast<std::int32_t>(area_id)) continue;
        if (root == kReNone || id == root || descends_from(r.parent, root)) return id;
    }
    return kReNone;
}

const net::ReRecord* RealEstateManager::find_up(const net::ReRecord* r, std::uint32_t mask) const {
    // FindChildAtPosition @0x4d6790 (depth 0): the record or the nearest parent whose type is in the mask, within 10
    // steps.
    for (int i = 0; r && i <= 10; ++i) {
        if (mask & re_type_bit(definition_type(r->definition))) return r;
        if (i >= 10) break;
        r = record(r->parent);
    }
    return nullptr;
}

const net::ReRecord* RealEstateManager::find_down(const net::ReRecord* r, std::uint32_t mask, int skip, int depth) const {
    // FUN_005b45b0: depth first through the child lists, `skip` levels left out, at most 10 deep.
    if (!r) return nullptr;
    if (skip < 1 && (mask & re_type_bit(definition_type(r->definition)))) return r;
    if (depth > 9) return nullptr;
    for (auto child : r->children)
        if (const net::ReRecord* hit = find_down(record(child), mask, skip - 1, depth + 1)) return hit;
    return nullptr;
}

void RealEstateManager::process(std::uint32_t now_ms, bool has_player, const glm::vec3& server_pos) {
    // FUN_005adb60 after 5 s: the latch is released whatever the answer.
    if (pending_ && now_ms - pending_at_ >= 5000) pending_ = false;
    if (records_.empty()) {
        occ_.current = occ_.root;
        occ_.yard = occ_.house = occ_.crate = kReNone;
        return;
    }
    if (ticked_ && now_ms - last_tick_ < 1000) return;
    ticked_ = true;
    last_tick_ = now_ms;
    // GetCurrentParentId @0x4b3040 → ResolveCurrentFromWorld @0x4b2f40 (only while the area manager exists, which
    // here is always): the first area holding the point whose estate is a plot or guild plot; else the root.
    std::uint32_t current = occ_.root;
    if (has_player) {
        for (const auto& a : areas_) {
            if (!point_in_area(a, server_pos)) continue;
            std::uint32_t id = area_estate(a.id, occ_.root);
            if (id != kReNone && re_is_owner_type(record_type(id))) {
                current = id;
                break;
            }
        }
    }
    occ_.current = current;
    const net::ReRecord* cur = record(current);
    // FUN_005affc0: the house is the current estate when it is one, else a house among a plot's children.
    occ_.house = kReNone;
    if (cur) {
        ReType t = definition_type(cur->definition);
        if (re_is_house_type(t)) {
            occ_.house = cur->id;
        } else if (re_is_owner_type(t)) {
            for (auto child : cur->children)
                if (const net::ReRecord* c = record(child); c && re_is_house_type(definition_type(c->definition))) {
                    occ_.house = c->id;
                    break;
                }
        }
    }
    // ResolveCurrentChild @0x5b00e0: the yard.
    const net::ReRecord* yard = find_up(cur, kReOwnerMask);
    occ_.yard = yard ? yard->id : kReNone;
    // FUN_005b0140: the first crate below the yard.
    const net::ReRecord* crate = find_down(yard, kReCrateMask, 1, 0);
    occ_.crate = crate ? crate->id : kReNone;
}

void RealEstateManager::reset() {
    *this = RealEstateManager();
}

std::uint32_t re_result_message(std::int32_t code) {
    // RealEstate::ShowResultCode @0x5ae1f0's arms (eqstr ids); 0 and -1 print nothing, an unlisted code 5223.
    switch (code) {
    case -1:
    case 0: return 0;
    case 9: return 5226;
    case 10: return 5219;
    case 11: return 5241;
    case 12: return 5272;
    case 15: return 5242;
    case 16: return 5243;
    case 17: return 5220;
    case 19: return 5229;
    case 20: return 5231;
    case 21: return 5275;
    case 22: return 5240;
    case 23: return 5251;
    case 24: return 6441;
    case 25: return 6442;
    case 28: return 5237;
    case 29: return 5273;
    case 31: return 6436;
    case 32: return 6437;
    case 33: return 6438;
    case 35: return 5250;
    case 36: return 5230;
    case 38: return 6440;
    case 39: return 5282;
    case 42: return 5283;
    case 43: return 5291;
    case 44: return 5292;
    case 45: return 5312;
    case 46: return 5313;
    case 47: return 5319;
    case 48: return 5331;
    case 49: return 5332;
    case 50: return 5333;
    case 53: return 5334;
    case 55: return 5335;
    case 59: return 5338;
    case 64: return 0x2db;
    case 65: return 0x2dc;
    case 66: return 0x2e7;
    case 67: return 0x2e8;
    case 68: return 0x2ed;
    case 70: return 0x2f8;
    case 71: return 5404;
    case 72: return 5405;
    case 73: return 5406;
    case 74: return 5407;
    case 75: return 5408;
    case 76: return 5409;
    case 77: return 5414;
    case 78: return 5415;
    case 79: return 5413;
    case 81: return 5418;
    case 82: return 5429;
    // 83 builds its line with FUN_007dbd10(.., 10, 1, 1, 1), not traced: nothing here (docs/unknowns.md).
    case 83: return 0;
    default: return 5223;
    }
}

std::uint32_t re_sell_message(std::int32_t code) {
    // FUN_005ad930; 13 builds its line with FUN_007dbd10 (not traced: nothing here).
    switch (code) {
    case 0:
    case 13: return 0;
    case 2: return 0x149d;
    case 3: return 0x1923;
    case 4: return 0x149c;
    case 5: return 0x14a1;
    case 6: return 0x152b;
    case 7: return 0x14c9;
    case 8: return 0x1537;
    case 9: return 0x2ef;
    case 10: return 0x2f0;
    case 11: return 0x152c;
    case 12: return 0x152d;
    default: return 0x149e;
    }
}

std::uint32_t re_transfer_message(std::int32_t code) {
    // FUN_005adad0.
    if (code == 0) return 0;
    if (code == 2) return 0x149f;
    if (code == 3) return 0x1dd5;
    return 0x14a0;
}

double re_upkeep_days(const RealEstateManager& m, const net::ReRecord& r) {
    // FUN_007413f0(definition, record +0x3c): the upkeep entry whose key is the record's upkeep set, within the
    // definition's upkeep group; its leaves are the daily cost per currency. RefreshUpkeepList: for each currency
    // with a cost, the escrow balance (the record's map; none 0) / the cost; the least wins; none with a cost:
    // unlimited (-1).
    const net::ReDefinition* d = m.definition(r.definition);
    const net::ReCostSet* set = d ? m.cost_set(d->upkeep_group) : nullptr;
    if (!set) return -1.0;
    const net::ReCostEntry* entry = nullptr;
    for (const auto& e : set->entries)
        if (e.key == r.upkeep_set) entry = &e;
    if (!entry) return -1.0;
    double days = -1.0;
    for (const auto& leaf : entry->leaves) {
        if (leaf.amount == 0) continue;
        double balance = 0;
        for (const auto& b : r.escrow)
            if (b.currency == leaf.currency) balance = b.amount;
        double v = balance / static_cast<double>(leaf.amount);
        if (days == -1.0 || v < days) days = v;
    }
    return days;
}

std::string re_format_time_left(double days) {
    // RealEstate_FormatTimeRemaining @0x740a50: "Unlimited" below 0, "Expired" at 0, else "%dw " (weeks, when any)
    // and "%dd" (the days past the weeks), "<1d" under a day.
    if (days < 0) return "Unlimited";
    if (days == 0) return "Expired";
    auto weeks = static_cast<long long>(days / 7.0);
    auto whole = static_cast<long long>(days);
    if (weeks == 0 && whole % 7 == 0) return "<1d";
    std::string s;
    if (weeks != 0) s = std::to_string(weeks) + "w ";
    return s + std::to_string(whole % 7) + "d";
}

} // namespace nmsc::game
