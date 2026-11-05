#include "game/mail.h"

#include <algorithm>
#include <cctype>
#include <optional>

namespace nmsc::game {

const char* mail_status_word(MailStatus s) {
    switch (s) {
    case MailStatus::New: return "NEW";
    case MailStatus::Unread: return "UNREAD";
    case MailStatus::Read: return "READ";
    case MailStatus::Trash: return "TRASH";
    case MailStatus::Deleted: return "DELETED";
    }
    return "NEW";
}

MailStatus mail_status_from_server(std::int32_t server_status) {
    switch (server_status) {
    case 2: return MailStatus::Unread;
    case 3: return MailStatus::Read;
    case 4: return MailStatus::Trash;
    case 5: return MailStatus::Deleted;
    default: return MailStatus::New;
    }
}

void MailState::store(MailHeader h) {
    // MailMgr_OnHeader @0x4698b0: new or unread marks the box unread; the highest id is kept.
    if (h.status == MailStatus::New || h.status == MailStatus::Unread) unread = true;
    max_id = std::max(max_id, h.id);
    auto it = headers.find(h.id);
    if (it != headers.end() && !h.body) {
        h.body = std::move(it->second.body);
        h.to = std::move(it->second.to);
    }
    headers[h.id] = std::move(h);
}

std::vector<const MailHeader*> MailState::inbox() const {
    std::vector<const MailHeader*> v;
    for (const auto& [id, h] : headers)
        if (h.status != MailStatus::Trash && h.status != MailStatus::Deleted) v.push_back(&h);
    std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->timestamp != b->timestamp ? a->timestamp > b->timestamp : a->id > b->id; });
    return v;
}

std::vector<const MailHeader*> MailState::trash() const {
    std::vector<const MailHeader*> v;
    for (const auto& [id, h] : headers)
        if (h.status == MailStatus::Trash) v.push_back(&h);
    std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->timestamp != b->timestamp ? a->timestamp > b->timestamp : a->id > b->id; });
    return v;
}

std::string mail_qualify_address(std::string address, const std::string& server) {
    auto dots = std::count(address.begin(), address.end(), '.');
    if (dots == 0) address = "SOE.EQ." + server + "." + address;
    else if (dots == 1) address = "SOE.EQ." + address;
    else if (dots == 2) address = "SOE." + address;
    std::replace(address.begin(), address.end(), ' ', '_');
    return address;
}

namespace {

bool iequal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool iless(std::string_view a, std::string_view b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string one_line(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

} // namespace

const MailAddressEntry* MailAddressBook::find(std::string_view name) const {
    name = trim(name);
    for (const auto& e : entries)
        if (iequal(e.name, name)) return &e;
    return nullptr;
}

bool MailAddressBook::put(MailAddressEntry e, const std::string& server) {
    e.name = one_line(std::string(trim(e.name)));
    if (e.name.empty()) return false;
    for (auto& a : e.addresses) a = one_line(std::move(a));
    std::erase_if(e.addresses, [](const std::string& a) { return a.empty(); });
    if (!server.empty())
        for (auto& a : e.addresses) a = mail_qualify_address(std::move(a), server);
    e.note = mail_normalize_note(e.note);
    auto same = std::find_if(entries.begin(), entries.end(), [&](const MailAddressEntry& x) { return iequal(x.name, e.name); });
    if (same != entries.end()) {
        // The entry keeps its place; its name takes the new spelling.
        *same = std::move(e);
        return true;
    }
    auto at = std::find_if(entries.begin(), entries.end(), [&](const MailAddressEntry& x) { return iless(e.name, x.name); });
    entries.insert(at, std::move(e));
    return true;
}

bool MailAddressBook::remove(std::string_view name) {
    name = trim(name);
    return std::erase_if(entries, [&](const MailAddressEntry& e) { return iequal(e.name, name); }) > 0;
}

namespace {

constexpr std::string_view kBookHeader = "EverQuest Address Book v1.0"; // 0x9c65c8, the static string at 0xabb740

} // namespace

std::string MailAddressBook::save_text() const {
    std::string out;
    out += kBookHeader;
    out += "\n# WARNING: To avoid errors, you should modify your address book using the game interface.\n"
           "# For those of you who like to live dangerously, just be aware that empty lines separate address book "
           "entries.\n"
           "# The game normalizes notes by inserting a space in empty lines.\n";
    for (const auto& e : entries) {
        out += "Name: " + e.name + "\nAddresses:";
        for (const auto& a : e.addresses) out += " " + a;
        out += e.is_private ? "\nFlags: PRIVATE\n" : "\nFlags: \n";
        out += "Note: " + e.note + "\n\n";
    }
    return out;
}

bool MailAddressBook::parse(std::string_view text, const std::string& server, MailAddressBook& out) {
    // std::getline over the file (opened as text, so a "\r\n" ends a line too).
    bool eof = false;
    auto getline = [&]() -> std::optional<std::string> {
        if (text.empty()) {
            eof = true;
            return std::nullopt;
        }
        std::size_t nl = text.find('\n');
        std::string line(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        if (nl == std::string_view::npos) eof = true;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    };
    auto starts = [](const std::string& s, std::string_view p) { return s.compare(0, p.size(), p) == 0; };
    std::string first = getline().value_or("");
    if (first != kBookHeader) {
        if (first.find('\t') == std::string::npos) return false;
        // This client's earlier file: "name<TAB>private 0/1<TAB>addresses<TAB>note" a line.
        for (std::optional<std::string> line = first; line; line = getline()) {
            std::string_view rest = *line, f[4];
            for (int i = 0; i < 4; ++i) {
                std::size_t tab = i < 3 ? rest.find('\t') : std::string_view::npos;
                f[i] = rest.substr(0, tab);
                rest = tab == std::string_view::npos ? std::string_view() : rest.substr(tab + 1);
            }
            out.put({std::string(f[0]), mail_split_addresses(f[2]), f[1] == "1", std::string(f[3])}, server);
        }
        return true;
    }
    // The comments and empty lines before the first entry.
    std::string line;
    while (!eof) {
        line = getline().value_or("");
        if (!line.empty() && line[0] != '#') break;
    }
    for (;;) {
        if (!starts(line, "Name: ")) return false;
        MailAddressEntry e;
        e.name = line.substr(6);
        // "Addresses:" and the addresses, read as words.
        line = getline().value_or("");
        std::vector<std::string> words = mail_split_addresses(line);
        if (words.empty() || words[0] != "Addresses:") return false;
        e.addresses.assign(words.begin() + 1, words.end());
        line = getline().value_or("");
        if (starts(line, "Flags: ")) {
            e.is_private = line.find("PRIVATE") != std::string::npos;
            line = getline().value_or("");
        }
        if (!starts(line, "Note: ")) return false;
        e.note = line.substr(6);
        while (auto more = getline()) {
            if (more->empty()) break;
            e.note += "\n" + *more;
        }
        out.put(std::move(e), server);
        line = getline().value_or("");
        if (eof) return true; // as the original, a last line without its line break is not read
    }
}

std::string mail_normalize_note(std::string_view note) {
    while (!note.empty() && (note.back() == ' ' || note.back() == '\n')) note.remove_suffix(1);
    std::string out;
    bool after_break = false;
    for (char c : note) {
        if (c == '\r') continue;
        if (c == '\n') {
            if (after_break) out += ' ';
            after_break = true;
        } else {
            after_break = false;
        }
        out += c;
    }
    return out;
}

std::vector<std::string> mail_split_addresses(std::string_view field) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < field.size()) {
        while (i < field.size() && (field[i] == ',' || field[i] == ' ' || field[i] == '\t' || field[i] == '\n' ||
                                    field[i] == '\r'))
            ++i;
        std::size_t b = i;
        while (i < field.size() && field[i] != ',' && field[i] != ' ' && field[i] != '\t' && field[i] != '\n' &&
               field[i] != '\r')
            ++i;
        if (i > b) out.emplace_back(field.substr(b, i - b));
    }
    return out;
}

std::string mail_expand_recipients(const std::string& list, const std::string& server, const MailAddressBook* book,
                                   const std::vector<MailAddressEntry>* groups) {
    auto lookup = [&](std::string_view name) -> const MailAddressEntry* {
        if (book)
            if (const auto* e = book->find(name)) return e;
        if (groups)
            for (const auto& g : *groups)
                if (iequal(g.name, trim(name))) return &g;
        return nullptr;
    };
    std::string out;
    std::size_t start = 0;
    while (start < list.size()) {
        std::size_t end = list.find(',', start);
        if (end == std::string::npos) end = list.size();
        std::size_t b = start;
        while (b < end && list[b] == ' ') ++b;
        bool hidden = b < end && list[b] == '-';
        if (hidden) ++b;
        std::string name = list.substr(b, end - b);
        while (!name.empty() && name.back() == ' ') name.pop_back(); // AddressBook_FindEntryByName trims
        if (!name.empty()) {
            if (const MailAddressEntry* e = name.find('<') == std::string::npos ? lookup(name) : nullptr) {
                // "Name <SOE.EQ.x.a SOE.EQ.x.b>"; an entry without addresses is dropped.
                std::string addrs;
                for (const auto& a : e->addresses) {
                    if (!addrs.empty()) addrs += ' ';
                    addrs += mail_qualify_address(a, server);
                }
                if (addrs.empty()) {
                    start = end + 1;
                    continue;
                }
                hidden = hidden || e->is_private;
                name = e->name + " <" + addrs + ">";
            } else if (name.find('<') == std::string::npos) {
                name = mail_qualify_address(name, server);
            }
            if (hidden) name.insert(name.begin(), '-');
            std::string item = out.empty() ? name : " " + name;
            if (out.find(item) == std::string::npos) {
                if (!out.empty()) out += ',';
                out += item;
            }
        }
        start = end + 1;
    }
    return out;
}

std::string mail_to_command(const std::string& recipients, const std::string& subject, const std::string& body,
                            const std::string& server, const MailAddressBook* book,
                            const std::vector<MailAddressEntry>* groups) {
    std::string s = "[mailto \"" + mail_expand_recipients(recipients, server, book, groups) + "\" ";
    s += subject.find(' ') != std::string::npos ? "\"" + subject + "\"" : subject; // Mail_QuoteIfHasSpace
    s += " ";
    s += body;
    return s;
}

std::string mail_short_name(const std::string& address) {
    auto dot = address.rfind('.');
    return dot == std::string::npos ? address : address.substr(dot + 1);
}

} // namespace nmsc::game
