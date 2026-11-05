// A compiled test plugin (tests/native_plugins_test.cpp), in C++ against the SDK alone: it calls the host table from
// where a plugin should not, to check that the client holds.
//   /probethread   calls host functions from a std::thread, then reports what they answered
//   /probethrow    lets an exception out (not a std::exception); so do ${Probe.Throw} and OnUpdateImGui
//   /probestate n  web_state(tab 1) with a struct whose size says n bytes, as a plugin built with a shorter one
// Built again with NMSC_PROBE_THROW_INIT (HostProbeThrow): InitializePlugin registers a command and a TLO, then
// throws.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "nmsc_plugin.h"

NMSC_PLUGIN

namespace {

void nothing(void*, const char*) {}

void probe_thread(void*, const char*) {
    int added = -1;
    std::uint32_t caps = 99;
    void* imgui = &added;
    std::thread t([&] {
        added = nmsc->add_command(nmsc->host, "/fromthread", nothing, nullptr);
        caps = nmsc->caps(nmsc->host);
        imgui = nmsc->imgui_context(nmsc->host);
        nmsc->write_chat(nmsc->host, "[Probe] written from the thread", 0);
        nmsc->do_command(nmsc->host, "/echo from the thread");
    });
    t.join();
    WriteChatf("[Probe] thread: add_command %d, caps %u, imgui %s", added, static_cast<unsigned>(caps),
               imgui ? "set" : "null");
}

void probe_throw(void*, const char*) { throw 42; }

void probe_state(void*, const char* args) {
    NmscWebState st;
    std::memset(&st, 0x5a, sizeof st);
    st.size = static_cast<size_t>(std::strtoul(args, nullptr, 10));
    const int r = nmsc->web_state(nmsc->host, 1, &st);
    WriteChatf("[Probe] state %d, size %u, url %s, title %s", r, static_cast<unsigned>(st.size),
               r && st.size > offsetof(NmscWebState, url) ? st.url : "-",
               static_cast<unsigned char>(st.title[0]) == 0x5a ? "untouched" : st.title);
}

int probe_tlo(void*, const char*, const char* member, const char*, NmscValue* out) {
    if (member && std::strcmp(member, "Throw") == 0) throw std::runtime_error("the TLO threw");
    out->kind = NMSC_STRING;
    out->s = "probe";
    return 1;
}

} // namespace

NMSC_API void InitializePlugin(void) {
    AddCommand("/probethread", probe_thread, nullptr);
    AddCommand("/probethrow", probe_throw, nullptr);
    AddCommand("/probestate", probe_state, nullptr);
    AddTopLevelObject("Probe", probe_tlo, nullptr);
#ifdef NMSC_PROBE_THROW_INIT
    throw std::runtime_error("InitializePlugin threw");
#endif
}
NMSC_API void ShutdownPlugin(void) {
    RemoveCommand("/probethread");
    RemoveCommand("/probethrow");
    RemoveCommand("/probestate");
    RemoveTopLevelObject("Probe");
}
NMSC_API void OnUpdateImGui(void) { throw std::runtime_error("OnUpdateImGui threw"); }
