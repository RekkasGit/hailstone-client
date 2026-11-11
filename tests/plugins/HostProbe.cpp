// A compiled test plugin (tests/native_plugins_test.cpp), in C++ against the SDK alone: it calls the host table from
// where a plugin should not, to check that the client holds.
//   /probethread   calls host functions from a std::thread, then reports what they answered
//   /probethrow    lets an exception out (not a std::exception); so do ${Probe.Throw} and OnUpdateImGui
//   /probestate n  web_state(tab 1) with a struct whose size says n bytes, as a plugin built with a shorter one
//   /probemessage n  web_message(tab 1) into a buffer of n bytes (0: no buffer)
//   /probetcp port    tcp_open("127.0.0.1", port); /probetcpsend h  tcp_send(h, "hello") and tcp_state(h)
//   /probebuffs n, /probepetbuffs n  get_buff_data_bulk / get_pet_buff_data_bulk into n bytes, read with ByteReader
// Built again with HAILSTONE_PROBE_THROW_INIT (HostProbeThrow): InitializePlugin registers a command and a TLO, then
// throws.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "hailstone_byte_io.h"
#include "hailstone_plugin.h"

HAILSTONE_PLUGIN

namespace {

void nothing(void*, const char*) {}

void probe_thread(void*, const char*) {
    int added = -1;
    std::uint32_t caps = 99;
    void* imgui = &added;
    std::thread t([&] {
        added = host->add_command(host->host, "/fromthread", nothing, nullptr);
        caps = host->caps(host->host);
        imgui = host->imgui_context(host->host);
        host->write_chat(host->host, "[Probe] written from the thread", 0);
        host->do_command(host->host, "/echo from the thread");
    });
    t.join();
    WriteChatf("[Probe] thread: add_command %d, caps %u, imgui %s", added, static_cast<unsigned>(caps),
               imgui ? "set" : "null");
}

void probe_throw(void*, const char*) { throw 42; }

void probe_state(void*, const char* args) {
    HailstoneWebState st;
    std::memset(&st, 0x5a, sizeof st);
    st.size = static_cast<size_t>(std::strtoul(args, nullptr, 10));
    const int r = host->web_state(host->host, 1, &st);
    WriteChatf("[Probe] state %d, size %u, url %s, title %s", r, static_cast<unsigned>(st.size),
               r && st.size > offsetof(HailstoneWebState, url) ? st.url : "-",
               static_cast<unsigned char>(st.title[0]) == 0x5a ? "untouched" : st.title);
}

void probe_message(void*, const char* args) {
    char buf[64];
    const std::size_t n = std::strtoul(args, nullptr, 10);
    const int r = host->web_message(host->host, 1, n ? buf : nullptr, n < sizeof buf ? n : sizeof buf);
    WriteChatf("[Probe] message %d, %s", r, n && r >= 0 ? buf : "-");
}

void probe_tcp(void*, const char* args) {
    const auto port = static_cast<std::uint16_t>(std::strtoul(args, nullptr, 10));
    WriteChatf("[Probe] tcp open %d", static_cast<int>(host->tcp_open(host->host, "127.0.0.1", port)));
}

void probe_tcp_send(void*, const char* args) {
    const auto handle = static_cast<std::int32_t>(std::strtol(args, nullptr, 10));
    const long long sent = host->tcp_send(host->host, handle, "hello", 5);
    WriteChatf("[Probe] tcp send %lld state %d", sent, static_cast<int>(host->tcp_state(host->host, handle)));
}

// The occupied slots one line each; the empty ones only counted, and only when laid out as empty.
void probe_buffs(void*, const char* args) {
    unsigned char buf[4096];
    const std::size_t n = std::strtoul(args, nullptr, 10);
    const std::int32_t r = host->get_buff_data_bulk(host->host, buf, n < sizeof buf ? n : sizeof buf);
    hailstone::ByteReader rd(buf, r > 0 ? static_cast<std::size_t>(r) : 0);
    int empty = 0;
    for (int slot = 0; rd.remaining() >= HAILSTONE_BUFF_RECORD_SIZE; ++slot) {
        const auto spell = rd.le<std::uint32_t>(), caster = rd.le<std::uint32_t>(), tics = rd.le<std::uint32_t>(),
                   hits = rd.le<std::uint32_t>(), type = rd.le<std::uint32_t>();
        const auto counter = rd.le<std::int32_t>(), base = rd.le<std::int32_t>(), left = rd.le<std::int32_t>();
        const auto song = rd.le<std::uint8_t>();
        if (spell == 0 && caster == 0 && tics == 0 && hits == 0 && type == 0 && counter == -1 && base == 0 &&
            left == 0 && song == 0) {
            ++empty;
            continue;
        }
        WriteChatf("[Probe] buff %d: %u %u %u %u %u %d %d %d %u", slot, static_cast<unsigned>(spell),
                   static_cast<unsigned>(caster), static_cast<unsigned>(tics), static_cast<unsigned>(hits),
                   static_cast<unsigned>(type), static_cast<int>(counter), static_cast<int>(base),
                   static_cast<int>(left), static_cast<unsigned>(song));
    }
    WriteChatf("[Probe] buffs %d bytes, %d empty", static_cast<int>(r), empty);
}

void probe_pet_buffs(void*, const char* args) {
    unsigned char buf[1024];
    const std::size_t n = std::strtoul(args, nullptr, 10);
    const std::int32_t r = host->get_pet_buff_data_bulk(host->host, buf, n < sizeof buf ? n : sizeof buf);
    hailstone::ByteReader rd(buf, r > 0 ? static_cast<std::size_t>(r) : 0);
    while (rd.remaining() >= HAILSTONE_PET_BUFF_RECORD_SIZE) {
        const auto slot = rd.le<std::uint32_t>(), spell = rd.le<std::uint32_t>(), tics = rd.le<std::uint32_t>(),
                   type = rd.le<std::uint32_t>();
        WriteChatf("[Probe] pet buff %u: %u %u %u", static_cast<unsigned>(slot), static_cast<unsigned>(spell),
                   static_cast<unsigned>(tics), static_cast<unsigned>(type));
    }
    WriteChatf("[Probe] pet buffs %d bytes", static_cast<int>(r));
}

int probe_tlo(void*, const char*, const char* member, const char*, HailstoneValue* out) {
    if (member && std::strcmp(member, "Throw") == 0) throw std::runtime_error("the TLO threw");
    out->kind = HAILSTONE_STRING;
    out->s = "probe";
    return 1;
}

} // namespace

HAILSTONE_API void InitializePlugin(void) {
    AddCommand("/probethread", probe_thread, nullptr);
    AddCommand("/probethrow", probe_throw, nullptr);
    AddCommand("/probestate", probe_state, nullptr);
    AddCommand("/probemessage", probe_message, nullptr);
    AddCommand("/probetcp", probe_tcp, nullptr);
    AddCommand("/probetcpsend", probe_tcp_send, nullptr);
    AddCommand("/probebuffs", probe_buffs, nullptr);
    AddCommand("/probepetbuffs", probe_pet_buffs, nullptr);
    AddTopLevelObject("Probe", probe_tlo, nullptr);
#ifdef HAILSTONE_PROBE_THROW_INIT
    throw std::runtime_error("InitializePlugin threw");
#endif
}
HAILSTONE_API void ShutdownPlugin(void) {
    RemoveCommand("/probethread");
    RemoveCommand("/probethrow");
    RemoveCommand("/probestate");
    RemoveCommand("/probemessage");
    RemoveCommand("/probetcp");
    RemoveCommand("/probetcpsend");
    RemoveCommand("/probebuffs");
    RemoveCommand("/probepetbuffs");
    RemoveTopLevelObject("Probe");
}
HAILSTONE_API void OnUpdateImGui(void) { throw std::runtime_error("OnUpdateImGui threw"); }
