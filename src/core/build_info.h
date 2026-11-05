#pragma once

// The build: "0.1.0-alpha (f6a07d7)", the release version and the commit it was built from ("-dirty" with
// uncommitted changes; the version alone without git). Made at each build (cmake/BuildInfo.cmake).

namespace nmsc {

const char* build_version();
// The release version alone ("0.1.0-alpha"; CMakeLists.txt NMSC_VERSION_LABEL): what the updater compares with a
// release's (core/version.h).
const char* release_version();

} // namespace nmsc
