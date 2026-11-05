#include "platform/message_box.h"

#include <SDL3/SDL_messagebox.h>

namespace nmsc::platform {

void show_error_box(const std::string& title, const std::string& text) {
    // SDL needs no initialised video for this; without a window it is a top-level box.
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr);
}

} // namespace nmsc::platform
