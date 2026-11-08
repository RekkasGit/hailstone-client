#pragma once

// The conversation journal (the NPC journal window's data; the original's ConversationJournal_* and the
// 0x47a0e0..0x47ba00 helpers, CJournalTextWnd +0x228): NPCs by name and zone, each with the lines they said to the
// player (an entry per distinct text: the time and place it was last said and a category), the categories, and the
// texts kept once each by their CRC (__MemChecker1).
//
// Files (ConversationJournal_Save @0x47b3d0 / _Load @0x47ba00): "<base>.txt" one text per line; "<base>.hdr"
// "EQJOURNAL", then "+CAT", id, name, description, "-CAT" for each category, then "+NPC", name, zone, "-NPC" and for
// each entry "+ENTRY", time "%08X", x, y, z, category, text CRC "%08X", "-ENTRY".

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace hailstone::game {

struct JournalEntry {
    std::int32_t time = 0;     // +0: when it was last said (time32)
    std::uint32_t text = 0;    // +4: the text's CRC
    std::int32_t x = 0, y = 0, z = 0; // +8..+0x10: where
    std::int32_t category = -1;       // +0x14: the category id, -1 none
};

struct JournalNpc {
    std::string name;          // +0 (63 characters at most)
    std::int32_t zone = 0;     // +0x40
    std::int32_t latest = 0;   // +0x44: the newest entry's time
    std::vector<std::unique_ptr<JournalEntry>> entries;

    // 0x47a0e0: the entry for `text`, made when new (true), else moved to the newer time and place.
    bool add_entry(std::int32_t time, float x, float y, float z, std::uint32_t text, std::int32_t category);
    // 0x47aab0: removes the entry of `text` (the newest time recomputed); its count after, -1 when none.
    int remove_entry(std::uint32_t text);
    // 0x47a0a0: the first entry from `from` in `category`, else null.
    const JournalEntry* entry_in_category(std::int32_t category, std::size_t from = 0) const;
};

struct JournalCategory {
    std::int32_t id = 0;       // +0
    std::string name;          // +4 (15 characters at most)
    std::string description;   // +0x14 (255 at most)
};

class ConversationJournal {
public:
    // 0x47b2d0: the NPC of (name, zone) (made when new), then its entry for the text (0x47b130: the text
    // kept by its CRC).
    void add(const std::string& npc, std::int32_t zone, std::int32_t time, float x, float y, float z,
             const std::string& text, std::int32_t category);
    // 0x47a4d0 / 0x47a3c0: the NPC of (name, zone), made when `create` and none.
    JournalNpc* npc(const std::string& name, std::int32_t zone, bool create);
    // 0x47ab60: removes the NPC of (name, zone).
    bool remove_npc(const std::string& name, std::int32_t zone);
    const std::vector<std::unique_ptr<JournalNpc>>& npcs() const { return npcs_; }

    // 0x47ac60 / 0x47ace0: the category `id` (made with the lowest free id, 0x47a550, when -1 or none),
    // named.
    JournalCategory* set_category(const std::string& name, const std::string& description, std::int32_t id);
    JournalCategory* category(std::int32_t id);                // 0x47a640
    JournalCategory* category_named(const std::string& name);  // 0x47a680 (exact)
    // 0x47a590: removes the category; its entries are left without one.
    void remove_category(std::int32_t id);
    const std::vector<std::unique_ptr<JournalCategory>>& categories() const { return categories_; }

    // 0x47b020: the text of a CRC ("" when unknown).
    const std::string& text(std::uint32_t crc) const;
    // 0x47b060: the first entry of `npc` from `from` whose text holds `upper` (upper case), -1 none.
    int find_text(const JournalNpc& npc, const std::string& upper, std::size_t from) const;

    // 0x47b1f0: empties it.
    void clear();
    // 0 loaded; -1 no .txt (a new journal); -2 a .txt but no .hdr.
    int load(const std::filesystem::path& base);
    // -1 when a file cannot be written.
    int save(const std::filesystem::path& base) const;

    static std::uint32_t crc(const std::string& text);

private:
    std::vector<std::unique_ptr<JournalNpc>> npcs_;
    std::vector<std::unique_ptr<JournalCategory>> categories_;
    std::unordered_map<std::uint32_t, std::string> texts_;
};

// Utf8ToUpper as the journal's searches use it (ASCII letters).
std::string journal_upper(std::string s);

} // namespace hailstone::game
