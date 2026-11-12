#include "platform/open_folder_dialog.h"

#ifdef _WIN32
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include <memory>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

namespace hailstone::platform {

#ifdef _WIN32
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// What became of the dialog: shown (the callback was called), or not made at all (the caller falls back to SDL).
enum class Outcome { Answered, NotMade };

Outcome run_dialog(SDL_DialogFileCallback callback, void* userdata, HWND owner, const std::wstring& start) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return Outcome::NotMade;
    FILEOPENDIALOGOPTIONS options = 0;
    if (FAILED(dialog->GetOptions(&options)) ||
        FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST))) {
        dialog->Release();
        return Outcome::NotMade;
    }
    if (!start.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }
    const HRESULT shown = dialog->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        const char* none[] = {nullptr};
        callback(userdata, none, -1);
        return Outcome::Answered;
    }
    std::string chosen;
    IShellItem* item = nullptr;
    if (SUCCEEDED(shown) && SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            chosen = narrow(path);
            CoTaskMemFree(path);
        }
        item->Release();
    }
    dialog->Release();
    if (chosen.empty()) {
        SDL_SetError("the folder dialog failed (0x%08lx)", static_cast<unsigned long>(shown));
        callback(userdata, nullptr, -1);
        return Outcome::Answered;
    }
    const char* list[] = {chosen.c_str(), nullptr};
    callback(userdata, list, -1);
    return Outcome::Answered;
}

// SDL's own dialog, for when the shell's cannot be made: SDL_ShowOpenFolderDialog must be called on the main thread,
// so the worker hands it there (SDL_RunOnMainThread runs it during event processing). The window is looked up again
// by id, as it may be gone by then.
struct Fallback {
    SDL_DialogFileCallback callback;
    void* userdata;
    SDL_WindowID window;
    std::string start;
};

void SDLCALL show_sdl_dialog(void* data) {
    std::unique_ptr<Fallback> f(static_cast<Fallback*>(data));
    SDL_Window* window = f->window ? SDL_GetWindowFromID(f->window) : nullptr;
    SDL_ShowOpenFolderDialog(f->callback, f->userdata, window, f->start.empty() ? nullptr : f->start.c_str(), false);
}

} // namespace
#endif

void show_open_folder_dialog(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const char* start) {
#ifdef _WIN32
    HWND owner = nullptr;
    if (window)
        owner = static_cast<HWND>(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    auto fallback = std::make_unique<Fallback>(
        Fallback{callback, userdata, window ? SDL_GetWindowID(window) : 0, start ? start : ""});
    // Its own thread, as SDL does: the dialog is modal and runs its own message loop, and COM wants a
    // single-threaded apartment for it, which the main thread need not be. The callback is called exactly once, on
    // that thread or (after the fallback) wherever SDL calls it; `userdata` is the caller's to keep alive until then
    // (both callers hand over a shared_ptr of their own, so the thread may outlive the window that opened it).
    std::thread([owner, f = std::move(fallback)]() mutable {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const Outcome outcome = run_dialog(f->callback, f->userdata, owner, widen(f->start));
        if (SUCCEEDED(com)) CoUninitialize();
        if (outcome == Outcome::Answered) return;
        if (!SDL_RunOnMainThread(&show_sdl_dialog, f.get(), false)) {
            f->callback(f->userdata, nullptr, -1); // SDL_RunOnMainThread set the error
            return;
        }
        static_cast<void>(f.release()); // show_sdl_dialog owns it now
    }).detach();
#else
    SDL_ShowOpenFolderDialog(callback, userdata, window, start, false);
#endif
}

} // namespace hailstone::platform
