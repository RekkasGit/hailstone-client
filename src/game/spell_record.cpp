#include "game/spell_record.h"

#include <charconv>
#include <cstring>

#include "core/float_chars.h"

namespace nmsc::game {

namespace {

struct Record {
    SpellRecord bytes{};
    void i32(std::uint32_t off, std::int32_t v) { std::memcpy(bytes.data() + off, &v, 4); }
    void u8(std::uint32_t off, std::uint8_t v) { bytes[off] = v; }
    void f32(std::uint32_t off, float v) { std::memcpy(bytes.data() + off, &v, 4); }
    void u16(std::uint32_t off, std::uint16_t v) { std::memcpy(bytes.data() + off, &v, 2); }
    std::int32_t get_i32(std::uint32_t off) const {
        std::int32_t v;
        std::memcpy(&v, bytes.data() + off, 4);
        return v;
    }
    float get_f32(std::uint32_t off) const {
        float v;
        std::memcpy(&v, bytes.data() + off, 4);
        return v;
    }
    void text(std::uint32_t off, std::string_view s) { std::memcpy(bytes.data() + off, s.data(), s.size()); }
};

struct Cursor {
    std::string_view line;
    std::size_t at = 0;
    char peek() const { return at < line.size() ? line[at] : '\0'; }
};

// ParseDelimitedField @0x808940: the characters up to the end, a line end or
// '^', at most size - 1 of them kept; the cursor moves past a '^' only.
std::string_view next_field(Cursor& c, std::size_t size) {
    std::size_t start = c.at;
    for (char ch = c.peek(); ch != 0 && ch != '\n' && ch != '\r' && ch != '^'; ch = c.peek()) ++c.at;
    std::string_view field = c.line.substr(start, c.at - start);
    if (c.peek() == '^') ++c.at;
    if (field.size() > size - 1) field = field.substr(0, size - 1);
    return field;
}

bool is_space(char ch) { return ch == ' ' || (ch >= '\t' && ch <= '\r'); }

// unk_NextCaretField @0x8089a0: atoi of the next field (511 characters at most).
std::int32_t next_int(Cursor& c) {
    std::string_view f = next_field(c, 0x200);
    std::size_t i = 0;
    while (i < f.size() && is_space(f[i])) ++i;
    bool negative = false;
    if (i < f.size() && (f[i] == '-' || f[i] == '+')) negative = f[i++] == '-';
    std::uint32_t v = 0;
    for (; i < f.size() && f[i] >= '0' && f[i] <= '9'; ++i) v = v * 10u + static_cast<std::uint32_t>(f[i] - '0');
    return static_cast<std::int32_t>(negative ? 0u - v : v);
}

// ParseDelimitedFloat @0x808a40: atof of the next field, stored to a float.
double next_double(Cursor& c) {
    std::string_view f = next_field(c, 0x200);
    std::size_t i = 0;
    while (i < f.size() && is_space(f[i])) ++i;
    bool negative = false;
    if (i < f.size() && (f[i] == '-' || f[i] == '+')) negative = f[i++] == '-';
    double v = 0.0;
    float_from_chars(f.data() + i, f.data() + f.size(), v);
    return negative ? -v : v;
}
float next_float(Cursor& c) { return static_cast<float>(next_double(c)); }

void next_text(Record& r, std::uint32_t off, std::size_t size, Cursor& c) { r.text(off, next_field(c, size)); }

// The float at +0x228, then the range ratio at +0x4e9 (outside the checksummed
// bytes): (value - [0x220]) / ([0x224] - [0x21c]), the divisor 1 when it lies
// strictly between -FLT_EPSILON and FLT_EPSILON (x87 precision, here double).
void range_ratio(Record& r, Cursor& c) {
    double v = next_double(c);
    r.f32(0x228, static_cast<float>(v));
    double d = static_cast<double>(r.get_f32(0x224)) - static_cast<double>(r.get_f32(0x21c));
    constexpr double kEps = 1.1920928955078125e-07;
    if (-kEps < d && d < kEps) d = 1.0;
    r.f32(0x4e9, static_cast<float>((v - static_cast<double>(r.get_f32(0x220))) / d));
}

void parse(Record& r, Cursor& c) {
    r.i32(0x174, next_int(c));
    next_text(r, 0x27a, 0x40, c);
    next_text(r, 0x2ba, 0x20, c);
    next_text(r, 0x2da, 0x20, c);
    next_text(r, 0x2fa, 0x60, c);
    next_text(r, 0x35a, 0x60, c);
    next_text(r, 0x3ba, 0x60, c);
    next_text(r, 0x41a, 0x60, c);
    next_text(r, 0x47a, 0x60, c);
    r.f32(0x0, next_float(c));
    r.f32(0x4, next_float(c));
    r.f32(0x8, next_float(c));
    r.f32(0xc, next_float(c));
    for (std::uint32_t i = 0; i < 43; ++i) {
        r.i32(0x10 + 4 * i, next_int(c));
    }
    r.i32(0x14c, next_int(c));
    r.i32(0x150, next_int(c));
    for (std::uint32_t i = 0; i < 24; ++i) {
        r.i32(0xbc + 4 * i, next_int(c));
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        r.u8(0x26a + i, static_cast<std::uint8_t>(next_int(c)));
    }
    for (std::uint32_t i = 0; i < 12; ++i) {
        r.i32(0x11c + 4 * i, next_int(c));
    }
    for (std::uint32_t i = 0; i < 6; ++i) {
        r.u8(0x26e + i, static_cast<std::uint8_t>(next_int(c)));
    }
    for (std::uint32_t i = 0; i < 16; ++i) {
        r.u8(0x247 + i, static_cast<std::uint8_t>(next_int(c)));
    }
    r.u8(0x274, static_cast<std::uint8_t>(next_int(c)));
    r.u8(0x276, static_cast<std::uint8_t>(next_int(c)));
    r.u8(0x277, static_cast<std::uint8_t>(next_int(c)));
    r.i32(0x160, next_int(c));
    r.u8(0x278, static_cast<std::uint8_t>(next_int(c)));
    r.i32(0x15c, 0);
    for (std::uint32_t i = 0; i < 17; ++i) {
        if (next_int(c) != 0) {
            r.i32(0x15c, static_cast<std::int32_t>(static_cast<std::uint32_t>(r.get_i32(0x15c)) | 1u << i));
        }
    }
    r.u8(0x4db, static_cast<std::uint8_t>(next_int(c)));
    next_int(c); // unused field
    r.i32(0x168, next_int(c));
    r.i32(0x164, next_int(c));
    r.u8(0x238, next_int(c) != 0 ? 1 : 0);
    r.i32(0x158, next_int(c));
    r.u8(0x239, next_int(c) != 0 ? 1 : 0);
    r.u8(0x23a, next_int(c) != 0 ? 1 : 0);
    r.i32(0x178, next_int(c));
    r.u8(0x233, next_int(c) != 0 ? 1 : 0);
    r.u8(0x236, next_int(c) != 0 ? 1 : 0);
    r.u8(0x235, next_int(c) != 0 ? 1 : 0);
    r.u8(0x237, next_int(c) != 0 ? 1 : 0);
    r.i32(0x154, next_int(c));
    if (static_cast<std::uint32_t>(r.get_i32(0x24)) > 0x36b0 && r.get_i32(0x11c) != 0x4c) {
        r.i32(0x24, 0x36b0);
    }
    for (std::uint32_t i = 0; i < 3; ++i) {
        r.i32(0x17c + 4 * i, next_int(c));
    }
    r.u8(0x230, next_int(c) != 0 ? 1 : 0);
    r.u8(0x231, next_int(c) != 0 ? 1 : 0);
    r.u8(0x232, next_int(c) != 0 ? 1 : 0);
    for (std::uint32_t i = 0; i < 3; ++i) {
        r.i32(0x188 + 4 * i, next_int(c));
    }
    r.u8(0x243, next_int(c) != 0 ? 1 : 0);
    r.i32(0x194, next_int(c));
    r.i32(0x198, next_int(c));
    r.u8(0x244, next_int(c) != 0 ? 1 : 0);
    for (std::uint32_t i = 0; i < 4; ++i) {
        next_int(c); // unused field
    }
    r.i32(0x1a0, next_int(c));
    r.i32(0x19c, next_int(c));
    r.i32(0x1a4, next_int(c));
    r.i32(0x1a8, next_int(c));
    for (std::uint32_t i = 0; i < 3; ++i) {
        r.i32(0x1b4 + 4 * i, next_int(c));
    }
    r.i32(0x1c8, next_int(c));
    r.i32(0x1c0, next_int(c));
    r.i32(0x1c4, next_int(c));
    r.i32(0x1cc, next_int(c));
    r.u8(0x240, next_int(c) != 0 ? 1 : 0);
    r.u8(0x241, next_int(c) != 0 ? 1 : 0);
    r.u8(0x242, next_int(c) != 0 ? 1 : 0);
    r.i32(0x1d0, next_int(c));
    r.i32(0x170, next_int(c));
    r.i32(0x1f4, next_int(c));
    r.i32(0x1f0, next_int(c));
    r.i32(0x1f8, next_int(c));
    r.i32(0x1fc, next_int(c));
    r.i32(0x16c, next_int(c));
    r.i32(0x1ac, next_int(c));
    r.i32(0x1b0, next_int(c));
    r.u8(0x4dc, next_int(c) != 0 ? 1 : 0);
    r.u8(0x4dd, next_int(c) != 0 ? 1 : 0);
    r.u8(0x4de, next_int(c) != 0 ? 1 : 0);
    r.u8(0x4da, next_int(c) != 0 ? 1 : 0);
    r.u8(0x279, next_int(c) != 0 ? 1 : 0);
    r.i32(0x200, next_int(c));
    r.i32(0x204, next_int(c));
    r.u8(0x4df, next_int(c) != 0 ? 1 : 0);
    next_int(c); // unused field
    r.u8(0x4e0, next_int(c) != 0 ? 1 : 0);
    r.u8(0x275, static_cast<std::uint8_t>(next_int(c)));
    r.i32(0x1d4, next_int(c));
    r.i32(0x1dc, next_int(c));
    r.u8(0x234, next_int(c) != 0 ? 1 : 0);
    r.u8(0x4e1, next_int(c) != 0 ? 1 : 0);
    r.i32(0x1e8, next_int(c));
    r.u8(0x23b, next_int(c) != 0 ? 1 : 0);
    r.u8(0x23c, next_int(c) != 0 ? 1 : 0);
    r.u8(0x23d, next_int(c) != 0 ? 1 : 0);
    r.u8(0x245, next_int(c) != 0 ? 1 : 0);
    next_int(c); // unused field
    r.i32(0x208, next_int(c));
    r.i32(0x20c, next_int(c));
    r.u8(0x23e, next_int(c) != 0 ? 1 : 0);
    r.i32(0x1ec, next_int(c));
    r.i32(0x1e0, next_int(c));
    r.i32(0x1e4, next_int(c));
    r.i32(0x210, next_int(c));
    r.u8(0x4e2, next_int(c) != 0 ? 1 : 0);
    r.f32(0x218, next_float(c));
    r.i32(0x214, next_int(c));
    r.f32(0x21c, next_float(c));
    r.f32(0x220, next_float(c));
    r.f32(0x224, next_float(c));
    range_ratio(r, c);
    r.f32(0x22c, next_float(c));
    r.u8(0x4e3, next_int(c) != 0 ? 1 : 0);
    r.i32(0x4e4, next_int(c));
    r.u8(0x23f, next_int(c) != 0 ? 1 : 0);
    next_int(c); // unused field
    r.i32(0x1d8, next_int(c));

}

} // namespace

SpellRecord spell_record_from_line(std::string_view line) {
    Record r;
    Cursor c{line};
    parse(r, c);
    return r.bytes;
}

SpellRecord unknown_spell_record() {
    Record r;
    r.u8(0x26b, 1);
    for (std::uint32_t i = 0; i < 4; ++i) {
        r.i32(0xbc + 4 * i, -1);
        r.i32(0xdc + 4 * i, -1);
    }
    r.f32(0x4, 0.0f);
    r.i32(0x160, -1);
    r.i32(0x150, 500);
    r.u8(0x26c, 0);
    r.i32(0x10, 1000);
    r.u8(0x274, 0x2b);
    r.i32(0x164, 0);
    r.u8(0x26a, 0);
    r.i32(0x24, 0);
    // NOLINTBEGIN(bugprone-string-constructor): the length includes the terminating NUL on purpose (fixed-size record text).
    r.text(0x27a, std::string_view("Unknown Spell", 14));
    r.u16(0x2ba, 0x20); // " "
    r.f32(0x0, 0.0f);
    r.f32(0x8, 0.0f);
    r.i32(0x14c, 0x21e);
    r.i32(0x18, 0);
    r.i32(0x1c, 0);
    r.i32(0x28, 0);
    r.u8(0x26d, 0);
    r.i32(0x2c, 0);
    r.i32(0xec, 0);
    r.i32(0x11c, 1);
    r.u8(0x26f, 0);
    for (std::uint32_t i = 0; i <= 0x10; ++i) r.u8(0x246 + i, 0xff);
    r.text(0x2fa, std::string_view("You cast Unknown Spell.", 24));
    r.text(0x35a, std::string_view(" casts Unknown Spell.", 22));
    r.text(0x3ba, std::string_view("You feel bogus as an unknown spell accosts you.", 48));
    r.text(0x41a, std::string_view(" is the victim of an unknown spell.", 36));
    r.text(0x47a, std::string_view("An unknown spell is gone.", 26));
    // NOLINTEND(bugprone-string-constructor)
    r.u8(0x26b, 0);
    for (std::uint32_t off : {0x154u, 0x17cu, 0x180u, 0x184u, 0x1a4u, 0x1a8u, 0x1b4u, 0x1b8u, 0x1bcu, 0x1c0u, 0x1c4u,
                              0x1ccu, 0x16cu, 0x1acu, 0x1b0u, 0x1d4u, 0x1d8u, 0x1dcu, 0x4e4u})
        r.i32(off, 0);
    r.u16(0x241, 0);
    r.u8(0x4da, 0);
    r.u16(0x278, 0x100);
    r.u8(0x275, 0xff);
    r.u8(0x234, 0);
    r.u16(0x23c, 0x101);
    r.u8(0x245, 1);
    r.u8(0x4e3, 0);
    r.u8(0x23f, 0);
    return r.bytes;
}

namespace {

constexpr std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xedb88320u : c >> 1;
        t[i] = c;
    }
    return t;
}
constexpr auto kCrcTable = make_crc_table(); // the exe's tables at 0xae42d0 and 0xb61908

} // namespace

std::uint32_t mem_checker0(std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xffffffffu;
    for (std::uint8_t b : data) crc = (crc >> 8) ^ kCrcTable[(b ^ crc) & 0xff];
    return crc;
}

std::uint32_t mem_checker1(std::span<const std::uint8_t> data) { return ~mem_checker0(data); }

std::uint32_t spell_record_crc(const SpellRecord& record) {
    std::uint32_t crc = mem_checker0(std::span<const std::uint8_t>(record.data(), kSpellCrcBytes));
    std::uint32_t b0 = crc & 0xff, b1 = crc >> 8 & 0xff, b2 = crc >> 16 & 0xff, b3 = crc >> 24;
    return b1 | b3 << 8 | b0 << 16 | b2 << 24;
}

} // namespace nmsc::game
