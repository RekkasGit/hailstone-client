#pragma once

// Where the in-game map's files come from (docs/layout.md "Maps"). The original reads one maps folder, the selected
// maps directory (`skin`) and then maps\ itself, file by file (MapViewMap_BuildMapFilePath @0x6c90f0). Hailstone has
// four sources, in this order:
//   own     servers/<profile>/maps: the player's edits on this server (where the toolbar saves)
//   server  the server's assets, servers/<profile>/assets/maps (the GameDir's overlay; downloaded or added by hand)
//   global  maps/ in the Hailstone folder: the player's maps for every server
//   game    the asset folder's maps (the GameDir's base and classic folder)
// A deliberate difference: a zone's four layers all come from one place, the first source that has any of them, so
// a server's map is never mixed with another source's layers drawn for other geometry. The selected maps directory
// is looked for in every source before the plain maps folder.

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nmsc {
class GameDir;
}

namespace nmsc::game {

struct MapFolders {
    std::filesystem::path own;     // empty: none
    const GameDir* game = nullptr; // the server's assets (its overlay) and the game's maps
    std::filesystem::path global;  // empty: none
};

enum class MapSource { None, Own, Server, Global, Game };

const char* map_source_name(MapSource s);

struct MapFiles {
    MapSource source = MapSource::None;
    std::string dir;                                        // "" maps itself, else the maps directory used
    std::array<std::optional<std::filesystem::path>, 4> layers; // layer 0..3, those the place has
};

// The files of `zone`'s map (layer names from map_layer_file_name), from the first place that has any of them.
MapFiles find_map_files(const MapFolders& f, std::string_view zone, std::string_view skin);

// The maps directories to choose from (CMapViewWnd_ScanMapFiles): the subfolders of maps in every source, one per
// name in any case, sorted by lower-cased name; those starting with '.' are left out.
std::vector<std::string> map_skins(const MapFolders& f);

} // namespace nmsc::game
