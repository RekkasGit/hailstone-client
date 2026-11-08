#pragma once

// The check of a folder the player picks as the asset folder (a RoF2 or a current live EverQuest install): the files every RoF2 install has and the client
// reads unconditionally, plus a few that only RoF2 has (to tell it from an older client). eqgame.exe is only a
// warning: it is read just for the login checksum, and a copy of the assets alone may not have it.

#include <filesystem>
#include <string>
#include <vector>

namespace hailstone {

class GameDir;

struct AssetFolderCheck {
    bool ok = false;                   // usable: a folder with nothing missing
    bool is_directory = false;         // false: the path is no folder at all (missing is then empty)
    std::vector<std::string> missing;  // required files not found, as relative paths ("snd*.pfs" for the sounds)
    std::vector<std::string> missing_rof2; // RoF2 markers not found: an older client's files, or not complete
    std::vector<std::string> warnings; // optional files not found, which do not stop play ("eqgame.exe")
    bool live = false;                 // a current live install rather than RoF2 (it has spells_us_str.txt)
};

// The required files, the RoF2 markers and the optional ones, relative to the asset folder.
const std::vector<std::string>& asset_folder_required_files();
const std::vector<std::string>& asset_folder_rof2_files();
const std::vector<std::string>& asset_folder_optional_files();

// Checks a folder on disk. Names compare case-insensitively; only the listed files are looked up (no full walk,
// so a wrong pick such as / is quick to reject).
AssetFolderCheck validate_asset_folder(const std::filesystem::path& folder);
// The same check through a GameDir's layers, for the merged view of a server overlay over the base.
AssetFolderCheck validate_asset_folder(const GameDir& game);

// Whether the asset folder (the base, not a server's overlay) is a current live EverQuest install rather than
// RoF2: it has spells_us_str.txt, which only the live client ships. The client reads either.
bool is_live_asset_folder(const GameDir& game);
// The files a RoF2 server's data must match (spells and their ids, the string ids the server sends, the skill caps
// and base stats, and GlobalLoad.txt, the character and equipment archives every zone has: RoF2's loads pbr_chr,
// live's bea_chr) that a live asset folder has only in its own, different versions. They should come from the
// server's assets folder; without them the live ones are read (spells_us.txt through spells_text_from_live; models
// GlobalLoad.txt leaves out through ModelArchiveStore).
const std::vector<std::string>& server_matched_files();
// With a live base: the server_matched_files() the overlay lacks (empty with a RoF2 base).
std::vector<std::string> missing_server_files(const GameDir& game);

// The classic folder (GameDir::set_classic): another install with the zone files a live base lacks or changed,
// such as EverQuest Legends. classic_zone_files() are the files a RoF2 server's zones need that the live client
// dropped or replaced (commons.s3d, misty.s3d, ...; bazaar.s3d, highpasshold.s3d, barter.eqg and
// freeporttemple.eqg are in no current install).
const std::vector<std::string>& classic_zone_files();
struct ClassicFolderCheck {
    bool ok = false;                 // a folder holding at least one of classic_zone_files()
    std::vector<std::string> has;     // the ones it holds
    std::vector<std::string> missing; // the ones it does not
};
ClassicFolderCheck check_classic_folder(const std::filesystem::path& folder);
// With a live base, the files whose classic folder copy comes before the base's: every top-level file (<zone>.s3d,
// <zone>_obj.s3d, <zone>.emt, <zone>_chr.txt, ...) of the zones whose live files differ from RoF2's where EverQuest
// Legends' match: everfrost (geometry and objects moved) and lavastorm (revamped; live keeps the new zone's
// emitters and lists). Live's nektulos and arena .s3d match RoF2's and Legends' nektulos does not, so those stay.
bool classic_preferred_file(std::string_view normalized);
// A GameDir on `base` with `classic` (when not empty) as its classic folder, preferred as apply_classic_folder does
// (the viewers' --classic-dir).
GameDir open_game_dir(const std::filesystem::path& base, const std::filesystem::path& classic);

// One line naming what is wrong ("" when ok), for the picker and the log.
std::string describe(const AssetFolderCheck& check);

} // namespace hailstone
