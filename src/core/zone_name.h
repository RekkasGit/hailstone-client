#pragma once

// Zone short names as file names. A zone's short name names its files (the zone's archives, the map layers, the
// navmesh cache), and it comes from the server (OP_NewZone), so a name that is a path could reach outside their
// folders. Stock zone short names are lower-case letters, digits and '_' ("qeynos2", "poknowledge", "freeportwest");
// upper case is taken as well, since the files are matched in any case.

#include <string_view>

#include "core/windows_names.h"

namespace hailstone {

// Whether `name` is a zone short name that is a plain file name everywhere: not empty, only ASCII letters, digits
// and '_', and not a Windows device name ("con", "lpt1").
inline bool is_zone_short_name(std::string_view name) {
    if (name.empty()) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return !is_windows_device_name(name);
}

} // namespace hailstone
