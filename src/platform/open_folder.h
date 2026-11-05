#pragma once

// Showing a folder to the player in the system's file manager, and an address in the system's web browser
// (SDL_OpenURL).

#include <filesystem>
#include <string>

namespace nmsc::platform {

// A file:// URL for an absolute path: '/' separators, a third '/' before a Windows drive ("file:///C:/EQ"), and
// every byte but letters, digits and -._~/: percent-encoded.
std::string file_url(const std::filesystem::path& path);
// Creates the folder when it is missing and opens it in the file manager; false when either fails.
bool open_folder(const std::filesystem::path& folder);
// Hands the address to the system (its web browser for http(s)); false, logged, when the system refuses it.
bool open_url(const std::string& url);

} // namespace nmsc::platform
