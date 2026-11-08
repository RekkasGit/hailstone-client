// A compiled test plugin (tests/native_plugins_test.cpp), in C++ against the SDK alone: it calls the host table from
// where a plugin should not, to check that the client holds.
//   /probethread   calls host functions from a std::thread, then reports what they answered
//   /probethrow    lets an exception out (not a std::exception); so do ${Probe.Throw} and OnUpdateImGui
//   /probestate n  web_state(tab 1) with a struct whose size says n bytes, as a plugin built with a shorter one
//   /probemessage n  web_message(tab 1) into a buffer of n bytes (0: no buffer)
// Built again with HAILSTONE_PROBE_THROW_INIT (HostProbeThrow): InitializePlugin registers a command and a TLO, then
// throws.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

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
    RemoveTopLevelObject("Probe");
}
HAILSTONE_API void OnUpdateImGui(void) { throw std::runtime_error("OnUpdateImGui threw"); }
