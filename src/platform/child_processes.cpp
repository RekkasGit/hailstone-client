#include "platform/child_processes.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_process.h>

#include <algorithm>

namespace nmsc::platform {

ChildProcesses::~ChildProcesses() {
    for (SDL_Process* p : children_) SDL_DestroyProcess(p); // the SDL object only, not the program
}

bool ChildProcesses::start(const std::vector<std::string>& args, std::string& error) {
    std::vector<const char*> argv;
    for (const auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_Process* p = SDL_CreateProcess(argv.data(), false);
    if (!p) {
        error = SDL_GetError();
        return false;
    }
    children_.push_back(p);
    return true;
}

void ChildProcesses::poll() {
    std::erase_if(children_, [](SDL_Process* p) {
        if (!SDL_WaitProcess(p, false, nullptr)) return false;
        SDL_DestroyProcess(p);
        return true;
    });
}

} // namespace nmsc::platform
