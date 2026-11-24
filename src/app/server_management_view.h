#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hailstone::app {

struct ServerManagementField {
    std::string section, key, value;
};
using ServerManagementEdit = ServerManagementField;

struct ServerManagementCommand {
    std::string name, description, origin;
    std::vector<std::string> aliases, groups;
};

// A snapshot authorized by the server. A matching serial needs only the metadata; the pack still has its fields.
struct ServerManagementView {
    std::string revision, file_revision, message;
    std::uint32_t serial = 0;
    int minimum_status = 200;
    bool saving = false;
    std::vector<ServerManagementField> fields;
    std::vector<ServerManagementCommand> commands;
};

} // namespace hailstone::app
