#include "game/pet_buttons.h"

#include <cctype>

namespace nmsc::game {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// The constructor's command index per button (0xb3a210; 0x13 none).
constexpr int kDefaultCommands[kPetWindowButtons] = {2, 4, 5, 9, 6, 7, 8, 15, 16, 19, 19, 19, 19, 19};

} // namespace

std::string_view pet_command_words(int index) {
    // eqstr 7512, 7521, 13092, 705, 713, 6841, 7518, 697, 6851, 7502, 7513, 6840, 694, 9299, 3272, 7500, 7504, 13103
    static constexpr std::string_view kWords[kPetCommandCount] = {
        "report health", "who leader", "attack",    "qattack", "follow", "guard",    "sit",      "stop",   "regroup",
        "taunt",         "hold",       "ghold",     "spellhold", "focus", "feign",   "back off", "get lost", "target"};
    return index >= 0 && index < kPetCommandCount ? kWords[index] : std::string_view{};
}

std::uint32_t pet_command_of_index(int index) {
    static constexpr std::uint32_t kCommands[kPetCommandCount] = {0,   1,   2,    3,    4,    5,    6,    9,    0x1f,
                                                                  0xc, 0xf, 0x12, 0x15, 0x18, 0x1b, 0x1c, 0x1d, 0x1e};
    return index >= 0 && index < kPetCommandCount ? kCommands[index] : 0x22;
}

int pet_command_index(std::string_view name) {
    for (int i = 0; i < kPetCommandCount; ++i)
        if (kPetCommandNames[static_cast<std::size_t>(i)] == name) return i;
    return kPetCommandNone;
}

int pet_command_toggle_state(int index) {
    // follow 0x916, guard 0x917, sit 0x913, stop 0x914, regroup 0x915, taunt 0x918, hold 0x919, ghold 0x91a,
    // spellhold 0x91c, focus 0x91b; Handle_OP_PetCommandState writes 0x913 + its index.
    static constexpr int kStates[10] = {3, 4, 0, 1, 2, 5, 6, 7, 9, 8};
    return index >= 4 && index <= 13 ? kStates[index - 4] : -1;
}

bool is_toggle_pet_command(std::string_view name) {
    for (std::string_view t : {"follow", "guard", "sit", "stop", "regroup", "taunt", "hold", "ghold", "spellhold", "focus"})
        if (name == t) return true;
    return false;
}

std::string pet_toggle_ini_key(int index) {
    return index >= 4 && index <= 13 ? "PetButtonToggleState" + std::to_string(index - 4) : std::string();
}

bool pet_toggle(const PetToggleStates& states, int index) {
    int st = pet_command_toggle_state(index);
    return st >= 0 && states[static_cast<std::size_t>(st)];
}

void set_pet_toggle(PetToggleStates& states, int index, bool on) {
    if (int st = pet_command_toggle_state(index); st >= 0) states[static_cast<std::size_t>(st)] = on;
}

bool pet_command_available(const PetButtonLayout& layout, std::uint32_t command) {
    // The command → the bPetButtonEnable byte (+0x900 + k) it reads.
    int k = -1;
    if (command <= 5) k = static_cast<int>(command);
    else if (command <= 8) k = 6;     // sit, sit on, sit off
    else if (command <= 0xb) k = 7;   // stop
    else if (command <= 0xe) k = 9;   // taunt
    else if (command <= 0x11) k = 10; // hold
    else if (command <= 0x14) k = 11; // ghold
    else if (command <= 0x17) k = 12; // spellhold
    else if (command <= 0x1a) k = 13; // focus
    else if (command <= 0x1e) k = static_cast<int>(command) - 0x1b + 14; // feign, back off, leave, target
    else if (command <= 0x21) k = 8;  // regroup
    else if (command == 0x22) return false;
    return k < 0 || layout.usable(k);
}

std::vector<std::uint32_t> pet_commands_to_reissue(const PetToggleStates& states,
                                                   const std::function<bool(std::uint32_t command)>& usable) {
    std::vector<std::uint32_t> out;
    for (int index = 4; index < 14; ++index) {
        std::uint32_t command = pet_command_of_index(index);
        if (pet_toggle(states, index) && usable && usable(command)) out.push_back(command);
    }
    return out;
}

bool can_use_pet_command(const PetCommandQuery& q, std::uint32_t command) {
    if (!q.has_pet) return false;
    auto effect = [&](std::int32_t spa, std::int32_t sub) { return q.effect && q.effect(spa, sub) != 0; };
    const auto sub = static_cast<std::int32_t>(command);
    if (q.race == 0x7f && q.summoned) {
        if (command > 0x21) return false;
        if (command == 0x1b) return effect(kSpaFeignDeath, 0);
        if (command == 0x1d) return true;
        return effect(kSpaPetCommand, sub);
    }
    if (q.property_1f) return command == 0x1d;
    if (command > 0x21) return false;
    switch (command) {
    case 0xc: case 0xd: case 0xe: case 0x1d:
        return q.summoned;
    case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a:
        return q.summoned && effect(kSpaPetCommand, sub);
    case 0x1b:
        return q.summoned && effect(kSpaFeignedMinion, 0);
    case 0xf: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x1f: case 0x20: case 0x21:
        return effect(kSpaPetCommand, sub);
    default: // 0..0xb, 0x1c, 0x1e
        return true;
    }
}

PetButtonLayout::PetButtonLayout() {
    map_.fill(-1);
    for (int b = 0; b < kPetWindowButtons; ++b) {
        int c = kDefaultCommands[b];
        if (c == kPetCommandNone) {
            text_[static_cast<std::size_t>(b)] = "none"; // the XML's text
        } else {
            text_[static_cast<std::size_t>(b)] = std::string(kPetCommandNames[static_cast<std::size_t>(c)]);
            map_[static_cast<std::size_t>(c)] = b;
        }
    }
}

int PetButtonLayout::command_of(int button) const {
    for (int i = 0; i < kPetCommandCount; ++i)
        if (map_[static_cast<std::size_t>(i)] == button) return i;
    return -1;
}

int PetButtonLayout::button_of(int index) const {
    return index >= 0 && index < kPetCommandCount ? map_[static_cast<std::size_t>(index)] : -1;
}

int PetButtonLayout::find(std::string_view text) const {
    if (iequals(text, "none")) return -1;
    for (int b = 0; b < kPetWindowButtons; ++b)
        if (iequals(text, text_[static_cast<std::size_t>(b)])) return b;
    return -1;
}

void PetButtonLayout::assign(int button, std::string_view command) {
    if (button < 0 || button >= kPetWindowButtons) return;
    if (int old = pet_command_index(text_[static_cast<std::size_t>(button)]); old < kPetCommandCount)
        map_[static_cast<std::size_t>(old)] = -1;
    if (int now = pet_command_index(command); now < kPetCommandCount) map_[static_cast<std::size_t>(now)] = button;
    // The button that held the text (the first by stricmp, maybe this one) holds none now. (The original's recursive
    // AssignCommandToButton(other, "none") then finds nothing: its text is already "none".)
    if (int other = find(command); other >= 0) {
        text_[static_cast<std::size_t>(other)] = "none";
        enabled_[static_cast<std::size_t>(other)] = false;
    }
}

void PetButtonLayout::choose(int button, int item) {
    if (button < 0 || button >= kPetWindowButtons) return;
    std::string command = item == kPetCommandNone || item < 1 || item > kPetCommandCount
                              ? std::string("none")
                              : std::string(kPetCommandNames[static_cast<std::size_t>(item - 1)]);
    assign(button, command);
    text_[static_cast<std::size_t>(button)] = command;
    if (command == "none") enabled_[static_cast<std::size_t>(button)] = false;
}

bool PetButtonLayout::load(std::string_view value) {
    if (value.empty()) return false;
    std::size_t pos = 0;
    for (int b = 0; b < kPetWindowButtons; ++b) {
        while (pos < value.size() && value[pos] == ' ') ++pos;
        if (pos >= value.size()) break; // strtok found no more words: the rest keep theirs
        std::size_t end = value.find(' ', pos);
        if (end == std::string_view::npos) end = value.size();
        std::string word(value.substr(pos, end - pos));
        pos = end;
        assign(b, word);
        text_[static_cast<std::size_t>(b)] = word;
    }
    return true;
}

std::string PetButtonLayout::save() const {
    std::string out;
    for (const auto& t : text_) out += t + " ";
    return out;
}

std::vector<PetButtonLayout::MenuRow> PetButtonLayout::menu() const {
    std::vector<MenuRow> rows;
    for (int i = 0; i < kPetCommandCount; ++i)
        if (usable_[static_cast<std::size_t>(i)]) rows.push_back({i + 1, kPetCommandNames[static_cast<std::size_t>(i)]});
    rows.push_back({kPetCommandNone, "none"});
    return rows;
}

void PetButtonLayout::sync_enabled() {
    for (int i = 0; i < kPetCommandCount; ++i) {
        int b = map_[static_cast<std::size_t>(i)];
        if (b >= 0 && !iequals(text_[static_cast<std::size_t>(b)], "none")) enabled_[static_cast<std::size_t>(b)] = true;
    }
}

std::optional<HotButton> pet_command_hot_button(int index, std::string_view pet_word, std::string_view text) {
    if (index < 0 || index >= kPetWindowButtons) return std::nullopt;
    HotButton b;
    b.type = HotButtonType::PetCommand;
    b.slot = index;
    // FormatString("%s %s"), then steve_strncpy(label, text, 0x40) on the drop.
    std::string label = std::string(pet_word) + " " + std::string(text);
    if (label.size() > 63) label.resize(63);
    b.label = std::move(label);
    return b;
}

std::optional<bool> pet_hot_button_checked(std::string_view label, std::string_view pet_word,
                                           std::span<const PetButtonFace> buttons) {
    std::optional<bool> out;
    for (const PetButtonFace& f : buttons) {
        if (f.text.empty() || iequals(f.text, "none")) continue;
        std::string want = std::string(pet_word) + " " + std::string(f.text);
        if (!iequals(label, want)) continue;
        out = is_toggle_pet_command(f.text) && f.checked;
    }
    return out;
}

} // namespace nmsc::game
